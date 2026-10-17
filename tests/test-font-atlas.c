/* test-font-atlas.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for LrgFontAtlas: headless baking of non-ASCII glyphs, growth,
 * the alpha threshold, lookups, the system font search and failures.
 * Baking needs an installed free font with Latin-1 coverage (found
 * through fontconfig or the XDG font directories); without one the
 * baking cases are skipped.
 */

#include <glib.h>
#include <libregnum.h>

/* a system font covering the accented letters the tests bake */
static gchar *
find_font (void)
{
    return lrg_font_atlas_find_system_font (NULL, "\xC3\xA9\xC3\xBC\xC3\xB1");
}

#define REQUIRE_FONT(path) \
    G_STMT_START { \
        if ((path) == NULL) \
        { \
            g_test_skip ("no installed TTF/OTF font covers Latin-1"); \
            return; \
        } \
    } G_STMT_END

static void
test_atlas_bake (void)
{
    g_autofree gchar *path = find_font ();
    g_autoptr(LrgFontAtlas) atlas = NULL;
    GError *error = NULL;
    GrlRectangle e_rect;
    GrlRectangle u_rect;
    GrlImage *image;
    guint8 *px;
    gsize size;
    gsize i;
    guint lit;

    REQUIRE_FONT (path);
    atlas = lrg_font_atlas_new_from_file (path, 10.0f, &error);
    g_assert_no_error (error);
    g_assert_nonnull (atlas);
    g_assert_cmpfloat (lrg_font_atlas_get_pixel_size (atlas), ==, 10.0f);
    g_assert_cmpfloat (lrg_font_atlas_get_line_height (atlas), >, 0.0f);
    g_assert_cmpfloat (lrg_font_atlas_get_ascent (atlas), >, 0.0f);

    /* empty: no image */
    g_assert_cmpuint (lrg_font_atlas_get_glyph_count (atlas), ==, 0);
    g_assert_null (lrg_font_atlas_get_image (atlas));

    /* spaces and repeats are not glyphs; the font covers each letter */
    g_assert_cmpuint (lrg_font_atlas_add_text (atlas, "\xC3\xA9 \xC3\xA9\xC3\xBC"), ==, 2);
    g_assert_cmpuint (lrg_font_atlas_add_text (atlas, "\xC3\xBC"), ==, 0);
    g_assert_true (lrg_font_atlas_has_glyph (atlas, 0xE9));
    g_assert_false (lrg_font_atlas_has_glyph (atlas, 0xF1));
    g_assert_true (lrg_font_atlas_font_has_glyph (atlas, 0xF1));

    g_assert_true (lrg_font_atlas_lookup (atlas, 0xE9, &e_rect));
    g_assert_true (lrg_font_atlas_lookup (atlas, 0xFC, &u_rect));
    g_assert_cmpfloat (e_rect.width, >, 0.0f);
    g_assert_cmpfloat (e_rect.height, >, 0.0f);
    /* the two cells do not overlap */
    g_assert_true (u_rect.x >= e_rect.x + e_rect.width || u_rect.y >= e_rect.y + e_rect.height);

    /* glyph pixels are white with coverage alpha */
    image = lrg_font_atlas_get_image (atlas);
    g_assert_nonnull (image);
    px = grl_image_get_pixels (image, &size);
    lit = 0;
    for (i = 0; i + 3 < size; i += 4)
    {
        if (px[i + 3] > 0)
            lit++;
    }
    g_assert_cmpuint (lit, >, 0);

    /* growing rebuilds and keeps the earlier glyphs */
    g_assert_cmpuint (lrg_font_atlas_add_text (atlas, "\xC3\xB1"), ==, 1);
    g_assert_cmpuint (lrg_font_atlas_get_glyph_count (atlas), ==, 3);
    g_assert_true (lrg_font_atlas_lookup (atlas, 0xF1, &u_rect));
    g_assert_true (lrg_font_atlas_lookup (atlas, 0xE9, &e_rect));
    g_assert_false (lrg_font_atlas_lookup (atlas, 0x4E00, &u_rect) &&
                    !lrg_font_atlas_has_glyph (atlas, 0x4E00));
}

static void
test_atlas_threshold (void)
{
    g_autofree gchar *path = find_font ();
    g_autoptr(LrgFontAtlas) atlas = NULL;
    GrlImage *image;
    guint8 *px;
    gsize size;
    gsize i;

    REQUIRE_FONT (path);
    atlas = lrg_font_atlas_new_from_file (path, 9.0f, NULL);
    g_assert_nonnull (atlas);
    lrg_font_atlas_set_alpha_threshold (atlas, 128);
    g_assert_cmpuint (lrg_font_atlas_get_alpha_threshold (atlas), ==, 128);
    lrg_font_atlas_add_text (atlas, "\xC3\xA9\xC3\xA0\xC3\xB6");
    image = lrg_font_atlas_get_image (atlas);
    g_assert_nonnull (image);
    px = grl_image_get_pixels (image, &size);
    /* every pixel is fully opaque or fully clear */
    for (i = 0; i + 3 < size; i += 4)
        g_assert_true (px[i + 3] == 0 || px[i + 3] == 255);
}

static void
test_atlas_invalid_input (void)
{
    g_autofree gchar *path = find_font ();
    g_autoptr(LrgFontAtlas) atlas = NULL;
    GError *error = NULL;
    GrlRectangle rect;

    /* a missing font file */
    atlas = lrg_font_atlas_new_from_file ("/no/such/font.ttf", 10.0f, &error);
    g_assert_null (atlas);
    g_assert_nonnull (error);
    g_clear_error (&error);

    /* a bad size */
    atlas = lrg_font_atlas_new_from_file ("/no/such/font.ttf", 0.0f, &error);
    g_assert_null (atlas);
    g_assert_error (error, LRG_FONT_ATLAS_ERROR, LRG_FONT_ATLAS_ERROR_INVALID_SIZE);
    g_clear_error (&error);

    REQUIRE_FONT (path);
    atlas = lrg_font_atlas_new_from_file (path, 10.0f, &error);
    g_assert_no_error (error);
    /* invalid UTF-8 bytes and control characters are skipped */
    g_assert_cmpuint (lrg_font_atlas_add_text (atlas, "\xC3\x28\x01\n"), ==, 1);
    g_assert_true (lrg_font_atlas_has_glyph (atlas, '('));
    g_assert_false (lrg_font_atlas_lookup (atlas, 0x10FFFF, &rect));
    g_assert_cmpuint (lrg_font_atlas_add_text (atlas, NULL), ==, 0);
}

static void
test_find_system_font (void)
{
    g_autofree gchar *path = find_font ();
    g_autofree gchar *none = NULL;

    /* no installed font has a glyph for an unassigned codepoint */
    none = lrg_font_atlas_find_system_font (NULL, "\xF3\xBF\xBF\xBD");
    g_assert_null (none);
    /* invalid needed text is refused */
    g_assert_null (lrg_font_atlas_find_system_font (NULL, "\xC3\x28"));

    REQUIRE_FONT (path);
    g_assert_true (g_file_test (path, G_FILE_TEST_IS_REGULAR));
    g_assert_true (g_str_has_suffix (path, ".ttf") || g_str_has_suffix (path, ".otf") ||
                   g_str_has_suffix (path, ".TTF") || g_str_has_suffix (path, ".OTF"));
}

int
main (int    argc,
      char **argv)
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/font-atlas/bake", test_atlas_bake);
    g_test_add_func ("/font-atlas/threshold", test_atlas_threshold);
    g_test_add_func ("/font-atlas/invalid-input", test_atlas_invalid_input);
    g_test_add_func ("/font-atlas/find-system-font", test_find_system_font);

    return g_test_run ();
}
