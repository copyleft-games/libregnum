/* test-text-baker.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgTextBaker.
 *
 * Tests that require a real TTF font on disk check for the DejaVu Sans font
 * file at its usual Fedora location.  If the file is absent the test is
 * skipped so the suite remains green on minimal CI images.
 *
 * Tests that upload textures to the GPU require a display / GL context and
 * are likewise skipped in headless environments.
 */

#include <glib.h>
#include <libregnum.h>
#include <raylib.h>  /* SetConfigFlags/FLAG_WINDOW_HIDDEN for a hidden GL context */

/*
 * Hidden-window GL context for GPU-upload (texture) tests; skipped in headless
 * CI where no window can be created. Mirrors the pattern in test-tilemap.c.
 */
#define SKIP_IF_NO_GRAPHICS() \
    do { \
        if (!graphics_available) { \
            g_test_skip ("Graphics context not available"); \
            return; \
        } \
    } while (0)

static gboolean   graphics_available = FALSE;
static GrlWindow *test_window = NULL;

static gboolean
init_graphics_context (void)
{
    const gchar *display = g_getenv ("DISPLAY");
    const gchar *wayland = g_getenv ("WAYLAND_DISPLAY");

    if ((display == NULL || display[0] == '\0') &&
        (wayland == NULL || wayland[0] == '\0'))
        return FALSE;

    SetConfigFlags (FLAG_WINDOW_HIDDEN);
    test_window = grl_window_new (1, 1, "lrg-test");

    if (test_window == NULL || !grl_window_is_ready (test_window))
    {
        g_clear_object (&test_window);
        return FALSE;
    }

    return TRUE;
}

static void
cleanup_graphics_context (void)
{
    g_clear_object (&test_window);
}

/* ==========================================================================
 * Constants
 * ========================================================================== */

#define DEJAVU_PATH "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"

/* ==========================================================================
 * Skip Helpers
 * ========================================================================== */

/*
 * SKIP_IF_NO_FONT:
 *
 * Skips the calling test if DejaVu Sans is not installed.
 */
#define SKIP_IF_NO_FONT() \
    do { \
        if (!g_file_test (DEJAVU_PATH, G_FILE_TEST_EXISTS)) \
        { \
            g_test_skip ("DejaVu Sans not installed (" DEJAVU_PATH ")"); \
            return; \
        } \
    } while (0)

/*
 * SKIP_IF_NO_DISPLAY:
 *
 * Skips the calling test when no display is available.  Required for tests
 * that upload textures via grl_texture_new_from_image().
 */
#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/*
 * SKIP_IF_NULL:
 *
 * Skips the calling test if @ptr is %NULL (e.g. resource unavailable).
 */
#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Resource not available"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Construction Tests
 * ========================================================================== */

static void
test_text_baker_new_from_file_bogus_path (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError) error = NULL;

    baker = lrg_text_baker_new_from_file ("/no/such/font.ttf", &error);

    g_assert_null (baker);
    /* Some kind of error must be set */
    g_assert_nonnull (error);
}

static void
test_text_baker_new_from_file_valid (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError) error = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);

    g_assert_nonnull (baker);
    g_assert_no_error (error);
    g_assert_true (LRG_IS_TEXT_BAKER (baker));
}

static void
test_text_baker_new_for_font (void)
{
    g_autoptr(GrlImageFont) font  = NULL;
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;

    SKIP_IF_NO_FONT ();

    font = grl_image_font_new_from_file (DEJAVU_PATH, &error);
    g_assert_nonnull (font);
    g_assert_no_error (error);

    baker = lrg_text_baker_new_for_font (font);
    g_assert_nonnull (baker);
    g_assert_true (LRG_IS_TEXT_BAKER (baker));

    /* get_font must return the same object (by pointer identity) */
    g_assert_true (lrg_text_baker_get_font (baker) == font);
}

/* ==========================================================================
 * Measurement Tests
 * ========================================================================== */

static void
test_text_baker_measure_basic (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    gboolean                ok;
    gint                    w = 0;
    gint                    h = 0;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    ok = lrg_text_baker_measure (baker, "Hello", 32.0f, &w, &h);

    g_assert_true (ok);
    g_assert_cmpint (w, >, 0);
    g_assert_cmpint (h, >, 0);
}

static void
test_text_baker_measure_null_text (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    gboolean                ok;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    /* NULL text must return FALSE gracefully, not crash */
    ok = lrg_text_baker_measure (baker, NULL, 32.0f, NULL, NULL);
    g_assert_false (ok);
}

static void
test_text_baker_measure_zero_px (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    gboolean                ok;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    ok = lrg_text_baker_measure (baker, "Hello", 0.0f, NULL, NULL);
    g_assert_false (ok);
}

static void
test_text_baker_measure_nullable_out (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    gboolean                ok;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    /* out params are both NULL – must not crash */
    ok = lrg_text_baker_measure (baker, "Hello", 32.0f, NULL, NULL);
    g_assert_true (ok);
}

/* ==========================================================================
 * render_to_image Tests
 * ========================================================================== */

static void
test_text_baker_render_to_image_transparent_bg (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     image = NULL;
    g_autoptr(GrlColor)     black = NULL;
    gint                    mw = 0;
    gint                    mh = 0;
    gint                    iw;
    gint                    ih;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    image = lrg_text_baker_render_to_image (baker, "Hi", 32.0f, black, NULL);
    g_assert_nonnull (image);

    iw = grl_image_get_width (image);
    ih = grl_image_get_height (image);

    g_assert_cmpint (iw, >, 0);
    g_assert_cmpint (ih, >, 0);

    /* Image size must match what measure() reports */
    lrg_text_baker_measure (baker, "Hi", 32.0f, &mw, &mh);
    g_assert_cmpint (iw, ==, mw);
    g_assert_cmpint (ih, ==, mh);
}

static void
test_text_baker_render_to_image_opaque_bg (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     image = NULL;
    g_autoptr(GrlColor)     black = NULL;
    g_autoptr(GrlColor)     white = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);
    white = grl_color_new (255, 255, 255, 255);

    image = lrg_text_baker_render_to_image (baker, "Hi", 32.0f, black, white);
    g_assert_nonnull (image);

    g_assert_cmpint (grl_image_get_width  (image), >, 0);
    g_assert_cmpint (grl_image_get_height (image), >, 0);
}

static void
test_text_baker_render_to_image_empty_text (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     image = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* Empty string must return NULL gracefully */
    image = lrg_text_baker_render_to_image (baker, "", 32.0f, black, NULL);
    g_assert_null (image);
}

static void
test_text_baker_render_to_image_zero_px (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     image = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* px_size == 0 must return NULL gracefully */
    image = lrg_text_baker_render_to_image (baker, "Hi", 0.0f, black, NULL);
    g_assert_null (image);
}

static void
test_text_baker_render_to_image_multiline (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     image_single = NULL;
    g_autoptr(GrlImage)     image_multi  = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    image_single = lrg_text_baker_render_to_image (baker, "Hello", 24.0f, black, NULL);
    image_multi  = lrg_text_baker_render_to_image (baker, "Hello\nWorld", 24.0f, black, NULL);

    g_assert_nonnull (image_single);
    g_assert_nonnull (image_multi);

    /* Two lines must be taller than one line */
    g_assert_cmpint (grl_image_get_height (image_multi),
                     >,
                     grl_image_get_height (image_single));
}

/* ==========================================================================
 * render_to_texture Tests
 * ========================================================================== */

static void
test_text_baker_render_to_texture (void)
{
    g_autoptr(LrgTextBaker) baker   = NULL;
    g_autoptr(GError)       error   = NULL;
    g_autoptr(GrlColor)     black   = NULL;
    GrlTexture             *texture = NULL;

    SKIP_IF_NO_FONT ();
    SKIP_IF_NO_GRAPHICS ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    texture = lrg_text_baker_render_to_texture (baker, "Hello", 32.0f, black, NULL);
    SKIP_IF_NULL (texture);

    /* Texture must be a valid GObject */
    g_assert_true (GRL_IS_TEXTURE (texture));

    g_object_unref (texture);
}

/* ==========================================================================
 * bake_atlas Tests
 * ========================================================================== */

static void
test_text_baker_bake_atlas_basic (void)
{
    g_autoptr(LrgTextBaker) baker   = NULL;
    g_autoptr(GError)       error   = NULL;
    g_autoptr(GrlImage)     atlas   = NULL;
    g_autoptr(GrlColor)     black   = NULL;
    GrlRectangle           *regions = NULL;
    guint                   n       = 0;
    guint                   i;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    atlas = lrg_text_baker_bake_atlas (baker, "AB", 32.0f, black, 2, &regions, &n);
    g_assert_nonnull (atlas);

    /* Exactly 2 glyphs (A and B), no duplicates */
    g_assert_cmpuint (n, ==, 2);
    g_assert_nonnull (regions);

    for (i = 0; i < n; i++)
    {
        /* Each region must have positive size */
        g_assert_cmpfloat (regions[i].width,  >, 0.0f);
        g_assert_cmpfloat (regions[i].height, >, 0.0f);

        /* Each region must lie within the atlas bounds */
        g_assert_cmpfloat (regions[i].x, >=, 0.0f);
        g_assert_cmpfloat (regions[i].y, >=, 0.0f);
        g_assert_cmpfloat (regions[i].x + regions[i].width,
                           <=,
                           (gfloat) grl_image_get_width (atlas));
        g_assert_cmpfloat (regions[i].y + regions[i].height,
                           <=,
                           (gfloat) grl_image_get_height (atlas));
    }

    g_free (regions);
}

static void
test_text_baker_bake_atlas_deduplication (void)
{
    g_autoptr(LrgTextBaker) baker   = NULL;
    g_autoptr(GError)       error   = NULL;
    g_autoptr(GrlImage)     atlas   = NULL;
    g_autoptr(GrlColor)     black   = NULL;
    GrlRectangle           *regions = NULL;
    guint                   n       = 0;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* "AAA" has only one unique codepoint -> one region */
    atlas = lrg_text_baker_bake_atlas (baker, "AAA", 32.0f, black, 1, &regions, &n);
    g_assert_nonnull (atlas);
    g_assert_cmpuint (n, ==, 1);
    g_assert_nonnull (regions);

    g_free (regions);
}

static void
test_text_baker_bake_atlas_empty_string (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     atlas = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* Empty codepoint string must return NULL gracefully */
    atlas = lrg_text_baker_bake_atlas (baker, "", 32.0f, black, 0, NULL, NULL);
    g_assert_null (atlas);
}

static void
test_text_baker_bake_atlas_zero_px (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     atlas = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* px_size == 0 must return NULL gracefully */
    atlas = lrg_text_baker_bake_atlas (baker, "AB", 0.0f, black, 0, NULL, NULL);
    g_assert_null (atlas);
}

static void
test_text_baker_bake_atlas_null_out_params (void)
{
    g_autoptr(LrgTextBaker) baker = NULL;
    g_autoptr(GError)       error = NULL;
    g_autoptr(GrlImage)     atlas = NULL;
    g_autoptr(GrlColor)     black = NULL;

    SKIP_IF_NO_FONT ();

    baker = lrg_text_baker_new_from_file (DEJAVU_PATH, &error);
    SKIP_IF_NULL (baker);

    black = grl_color_new (0, 0, 0, 255);

    /* NULL out_regions / out_n must not crash */
    atlas = lrg_text_baker_bake_atlas (baker, "AB", 32.0f, black, 1, NULL, NULL);
    g_assert_nonnull (atlas);
}

/* ==========================================================================
 * Main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    /* Construction */
    g_test_add_func ("/text-baker/new-from-file/bogus-path",
                     test_text_baker_new_from_file_bogus_path);
    g_test_add_func ("/text-baker/new-from-file/valid",
                     test_text_baker_new_from_file_valid);
    g_test_add_func ("/text-baker/new-for-font",
                     test_text_baker_new_for_font);

    /* Measurement */
    g_test_add_func ("/text-baker/measure/basic",
                     test_text_baker_measure_basic);
    g_test_add_func ("/text-baker/measure/null-text",
                     test_text_baker_measure_null_text);
    g_test_add_func ("/text-baker/measure/zero-px",
                     test_text_baker_measure_zero_px);
    g_test_add_func ("/text-baker/measure/nullable-out",
                     test_text_baker_measure_nullable_out);

    /* render_to_image */
    g_test_add_func ("/text-baker/render-to-image/transparent-bg",
                     test_text_baker_render_to_image_transparent_bg);
    g_test_add_func ("/text-baker/render-to-image/opaque-bg",
                     test_text_baker_render_to_image_opaque_bg);
    g_test_add_func ("/text-baker/render-to-image/empty-text",
                     test_text_baker_render_to_image_empty_text);
    g_test_add_func ("/text-baker/render-to-image/zero-px",
                     test_text_baker_render_to_image_zero_px);
    g_test_add_func ("/text-baker/render-to-image/multiline",
                     test_text_baker_render_to_image_multiline);

    /* render_to_texture */
    g_test_add_func ("/text-baker/render-to-texture",
                     test_text_baker_render_to_texture);

    /* bake_atlas */
    g_test_add_func ("/text-baker/bake-atlas/basic",
                     test_text_baker_bake_atlas_basic);
    g_test_add_func ("/text-baker/bake-atlas/deduplication",
                     test_text_baker_bake_atlas_deduplication);
    g_test_add_func ("/text-baker/bake-atlas/empty-string",
                     test_text_baker_bake_atlas_empty_string);
    g_test_add_func ("/text-baker/bake-atlas/zero-px",
                     test_text_baker_bake_atlas_zero_px);
    g_test_add_func ("/text-baker/bake-atlas/null-out-params",
                     test_text_baker_bake_atlas_null_out_params);

    result = g_test_run ();
    cleanup_graphics_context ();
    return result;
}
