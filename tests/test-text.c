/* test-text.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for rich text module.
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

/* RichText fixture */
typedef struct
{
    LrgRichText *text;
} RichTextFixture;

static void
rich_text_fixture_set_up (RichTextFixture *fixture,
                          gconstpointer    user_data)
{
    (void) user_data;
    fixture->text = lrg_rich_text_new ();
}

static void
rich_text_fixture_tear_down (RichTextFixture *fixture,
                             gconstpointer    user_data)
{
    (void) user_data;
    g_clear_object (&fixture->text);
}

/*
 * ============================================================================
 * LrgTextSpan Tests
 * ============================================================================
 */

static void
test_span_new (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Hello");

    g_assert_nonnull (span);
    g_assert_cmpstr (lrg_text_span_get_text (span), ==, "Hello");

    lrg_text_span_free (span);
}

static void
test_span_copy (void)
{
    LrgTextSpan *span = NULL;
    LrgTextSpan *copy = NULL;

    span = lrg_text_span_new ("Test");
    lrg_text_span_set_font_size (span, 2.0f);

    copy = lrg_text_span_copy (span);

    g_assert_cmpstr (lrg_text_span_get_text (copy), ==, "Test");
    g_assert_cmpfloat_with_epsilon (lrg_text_span_get_font_size (copy), 2.0f, 0.001f);

    lrg_text_span_free (span);
    lrg_text_span_free (copy);
}

static void
test_span_text (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Original");

    lrg_text_span_set_text (span, "Modified");
    g_assert_cmpstr (lrg_text_span_get_text (span), ==, "Modified");

    lrg_text_span_free (span);
}

static void
test_span_style (void)
{
    LrgTextSpan *span = NULL;
    LrgTextStyle style;

    span = lrg_text_span_new ("Styled");

    /* Set bold and italic */
    lrg_text_span_set_style (span, LRG_TEXT_STYLE_BOLD | LRG_TEXT_STYLE_ITALIC);
    style = lrg_text_span_get_style (span);

    g_assert_true (style & LRG_TEXT_STYLE_BOLD);
    g_assert_true (style & LRG_TEXT_STYLE_ITALIC);
    g_assert_false (style & LRG_TEXT_STYLE_UNDERLINE);

    lrg_text_span_free (span);
}

static void
test_span_color (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Colored");
    guint8 r, g, b, a;

    lrg_text_span_set_color (span, 255, 128, 64, 200);
    lrg_text_span_get_color (span, &r, &g, &b, &a);

    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 64);
    g_assert_cmpuint (a, ==, 200);

    lrg_text_span_free (span);
}

static void
test_span_color_hex (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Hex");
    guint8 r, g, b, a;

    /* With hash */
    g_assert_true (lrg_text_span_set_color_hex (span, "#FF8040"));
    lrg_text_span_get_color (span, &r, &g, &b, &a);
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 128);
    g_assert_cmpuint (b, ==, 64);

    /* Without hash */
    g_assert_true (lrg_text_span_set_color_hex (span, "00FF00"));
    lrg_text_span_get_color (span, &r, &g, &b, &a);
    g_assert_cmpuint (r, ==, 0);
    g_assert_cmpuint (g, ==, 255);
    g_assert_cmpuint (b, ==, 0);

    lrg_text_span_free (span);
}

static void
test_span_font_size (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Big");

    /* Default should be 1.0 */
    g_assert_cmpfloat_with_epsilon (lrg_text_span_get_font_size (span), 1.0f, 0.001f);

    lrg_text_span_set_font_size (span, 2.5f);
    g_assert_cmpfloat_with_epsilon (lrg_text_span_get_font_size (span), 2.5f, 0.001f);

    lrg_text_span_free (span);
}

static void
test_span_effect (void)
{
    LrgTextSpan *span = lrg_text_span_new ("Effect");

    lrg_text_span_set_effect_type (span, LRG_TEXT_EFFECT_SHAKE);
    g_assert_cmpint (lrg_text_span_get_effect_type (span), ==, LRG_TEXT_EFFECT_SHAKE);

    lrg_text_span_set_effect_speed (span, 2.0f);
    g_assert_cmpfloat_with_epsilon (lrg_text_span_get_effect_speed (span), 2.0f, 0.001f);

    lrg_text_span_free (span);
}

/*
 * ============================================================================
 * LrgTextEffect Tests
 * ============================================================================
 */

static void
test_effect_new (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_WAVE);

    g_assert_nonnull (effect);
    g_assert_cmpint (lrg_text_effect_get_effect_type (effect), ==, LRG_TEXT_EFFECT_WAVE);
}

static void
test_effect_speed (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_SHAKE);

    lrg_text_effect_set_speed (effect, 3.0f);
    g_assert_cmpfloat_with_epsilon (lrg_text_effect_get_speed (effect), 3.0f, 0.001f);
}

static void
test_effect_intensity (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_WAVE);

    lrg_text_effect_set_intensity (effect, 0.75f);
    g_assert_cmpfloat_with_epsilon (lrg_text_effect_get_intensity (effect), 0.75f, 0.001f);
}

static void
test_effect_update (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_SHAKE);

    g_assert_cmpfloat_with_epsilon (lrg_text_effect_get_time (effect), 0.0f, 0.001f);

    lrg_text_effect_update (effect, 0.5f);
    g_assert_cmpfloat (lrg_text_effect_get_time (effect), >, 0.0f);
}

static void
test_effect_reset (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_WAVE);

    lrg_text_effect_update (effect, 1.0f);
    g_assert_cmpfloat (lrg_text_effect_get_time (effect), >, 0.0f);

    lrg_text_effect_reset (effect);
    g_assert_cmpfloat_with_epsilon (lrg_text_effect_get_time (effect), 0.0f, 0.001f);
}

static void
test_effect_typewriter (void)
{
    g_autoptr(LrgTextEffect) effect = lrg_text_effect_new (LRG_TEXT_EFFECT_TYPEWRITER);

    lrg_text_effect_set_char_count (effect, 10);
    g_assert_false (lrg_text_effect_is_complete (effect));

    /* Update enough to complete */
    lrg_text_effect_update (effect, 10.0f);
    g_assert_true (lrg_text_effect_is_complete (effect));
}

/*
 * ============================================================================
 * LrgRichText Tests
 * ============================================================================
 */

static void
test_rich_text_new (void)
{
    g_autoptr(LrgRichText) text = lrg_rich_text_new ();

    g_assert_nonnull (text);
    g_assert_cmpuint (lrg_rich_text_get_span_count (text), ==, 0);
}

static void
test_rich_text_parse_plain (RichTextFixture *fixture,
                            gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "Hello World");

    g_assert_cmpstr (lrg_rich_text_get_plain_text (fixture->text), ==, "Hello World");
    g_assert_cmpuint (lrg_rich_text_get_span_count (fixture->text), ==, 1);
}

static void
test_rich_text_parse_bold (RichTextFixture *fixture,
                           gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "Normal [b]Bold[/b] Text");

    g_assert_cmpstr (lrg_rich_text_get_plain_text (fixture->text), ==, "Normal Bold Text");
    /* Should have 3 spans: "Normal ", "Bold", " Text" */
    g_assert_cmpuint (lrg_rich_text_get_span_count (fixture->text), >=, 1);
}

static void
test_rich_text_parse_italic (RichTextFixture *fixture,
                             gconstpointer    user_data)
{
    const LrgTextSpan *span = NULL;
    LrgTextStyle style;

    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[i]Italic[/i]");

    span = lrg_rich_text_get_span (fixture->text, 0);
    g_assert_nonnull (span);

    style = lrg_text_span_get_style (span);
    g_assert_true (style & LRG_TEXT_STYLE_ITALIC);
}

static void
test_rich_text_parse_color (RichTextFixture *fixture,
                            gconstpointer    user_data)
{
    const LrgTextSpan *span = NULL;
    guint8 r, g, b, a;

    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[color=#FF0000]Red[/color]");

    span = lrg_rich_text_get_span (fixture->text, 0);
    g_assert_nonnull (span);

    lrg_text_span_get_color (span, &r, &g, &b, &a);
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 0);
    g_assert_cmpuint (b, ==, 0);
}

static void
test_rich_text_parse_size (RichTextFixture *fixture,
                           gconstpointer    user_data)
{
    const LrgTextSpan *span = NULL;

    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[size=2.0]Big[/size]");

    span = lrg_rich_text_get_span (fixture->text, 0);
    g_assert_nonnull (span);
    g_assert_cmpfloat_with_epsilon (lrg_text_span_get_font_size (span), 2.0f, 0.001f);
}

static void
test_rich_text_parse_effects (RichTextFixture *fixture,
                              gconstpointer    user_data)
{
    const LrgTextSpan *span = NULL;

    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[shake]Shaky[/shake]");

    span = lrg_rich_text_get_span (fixture->text, 0);
    g_assert_nonnull (span);
    g_assert_cmpint (lrg_text_span_get_effect_type (span), ==, LRG_TEXT_EFFECT_SHAKE);
}

static void
test_rich_text_font_size (RichTextFixture *fixture,
                          gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_font_size (fixture->text, 24.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rich_text_get_font_size (fixture->text), 24.0f, 0.001f);
}

static void
test_rich_text_line_spacing (RichTextFixture *fixture,
                             gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_line_spacing (fixture->text, 1.5f);
    g_assert_cmpfloat_with_epsilon (lrg_rich_text_get_line_spacing (fixture->text), 1.5f, 0.001f);
}

static void
test_rich_text_max_width (RichTextFixture *fixture,
                          gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_max_width (fixture->text, 400.0f);
    g_assert_cmpfloat_with_epsilon (lrg_rich_text_get_max_width (fixture->text), 400.0f, 0.001f);
}

static void
test_rich_text_alignment (RichTextFixture *fixture,
                          gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_alignment (fixture->text, LRG_TEXT_ALIGN_CENTER);
    g_assert_cmpint (lrg_rich_text_get_alignment (fixture->text), ==, LRG_TEXT_ALIGN_CENTER);

    lrg_rich_text_set_alignment (fixture->text, LRG_TEXT_ALIGN_RIGHT);
    g_assert_cmpint (lrg_rich_text_get_alignment (fixture->text), ==, LRG_TEXT_ALIGN_RIGHT);
}

static void
test_rich_text_default_color (RichTextFixture *fixture,
                              gconstpointer    user_data)
{
    guint8 r, g, b, a;

    (void) user_data;

    lrg_rich_text_set_default_color (fixture->text, 200, 150, 100, 255);
    lrg_rich_text_get_default_color (fixture->text, &r, &g, &b, &a);

    g_assert_cmpuint (r, ==, 200);
    g_assert_cmpuint (g, ==, 150);
    g_assert_cmpuint (b, ==, 100);
    g_assert_cmpuint (a, ==, 255);
}

static void
test_rich_text_update (RichTextFixture *fixture,
                       gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[shake]Test[/shake]");

    /* Should not crash */
    lrg_rich_text_update (fixture->text, 0.016f);
    lrg_rich_text_update (fixture->text, 0.016f);
}

static void
test_rich_text_reset_effects (RichTextFixture *fixture,
                              gconstpointer    user_data)
{
    (void) user_data;

    lrg_rich_text_set_markup (fixture->text, "[typewriter]Test[/typewriter]");
    lrg_rich_text_update (fixture->text, 5.0f);

    lrg_rich_text_reset_effects (fixture->text);

    /* After reset, effects should not be complete */
    g_assert_false (lrg_rich_text_effects_complete (fixture->text));
}

static void
test_rich_text_new_from_markup (void)
{
    g_autoptr(LrgRichText) text = lrg_rich_text_new_from_markup ("[b]Bold[/b] and [i]italic[/i]");

    g_assert_nonnull (text);
    g_assert_cmpstr (lrg_rich_text_get_plain_text (text), ==, "Bold and italic");
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

    /* LrgTextSpan tests */
    g_test_add_func ("/text/span/new", test_span_new);
    g_test_add_func ("/text/span/copy", test_span_copy);
    g_test_add_func ("/text/span/text", test_span_text);
    g_test_add_func ("/text/span/style", test_span_style);
    g_test_add_func ("/text/span/color", test_span_color);
    g_test_add_func ("/text/span/color-hex", test_span_color_hex);
    g_test_add_func ("/text/span/font-size", test_span_font_size);
    g_test_add_func ("/text/span/effect", test_span_effect);

    /* LrgTextEffect tests */
    g_test_add_func ("/text/effect/new", test_effect_new);
    g_test_add_func ("/text/effect/speed", test_effect_speed);
    g_test_add_func ("/text/effect/intensity", test_effect_intensity);
    g_test_add_func ("/text/effect/update", test_effect_update);
    g_test_add_func ("/text/effect/reset", test_effect_reset);
    g_test_add_func ("/text/effect/typewriter", test_effect_typewriter);

    /* LrgRichText tests */
    g_test_add_func ("/text/rich-text/new", test_rich_text_new);
    g_test_add_func ("/text/rich-text/new-from-markup", test_rich_text_new_from_markup);
    g_test_add ("/text/rich-text/parse-plain", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_plain, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/parse-bold", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_bold, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/parse-italic", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_italic, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/parse-color", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_color, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/parse-size", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_size, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/parse-effects", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_parse_effects, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/font-size", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_font_size, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/line-spacing", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_line_spacing, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/max-width", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_max_width, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/alignment", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_alignment, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/default-color", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_default_color, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/update", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_update, rich_text_fixture_tear_down);
    g_test_add ("/text/rich-text/reset-effects", RichTextFixture, NULL,
                rich_text_fixture_set_up, test_rich_text_reset_effects, rich_text_fixture_tear_down);

    return g_test_run ();
}
