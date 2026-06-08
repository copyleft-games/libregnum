/* test-vector-image.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgVectorImage.
 *
 * All tests use SVG strings passed via lrg_vector_image_new_from_data()
 * so that no external files are required.  The tests are headless-safe:
 * rasterization is CPU-side (GrlImage software rasterizer) and does not
 * need an OpenGL context.  render_to_texture *does* need a GL context
 * (grl_texture_new_from_image uploads to the GPU), so that test is
 * skipped when no display is available.
 */

#include <glib.h>
#include <libregnum.h>
#include <math.h>
#include <raylib.h>  /* SetConfigFlags/FLAG_WINDOW_HIDDEN for a hidden GL context */

/*
 * Hidden-window GL context for GPU-upload (render_to_texture) tests; skipped in
 * headless CI where no window can be created. Mirrors test-tilemap.c.
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
 * Headless helpers
 * ========================================================================== */

/*
 * SKIP_IF_NO_DISPLAY:
 *
 * Skip the current test when running without a graphical display.
 * Used for tests that indirectly need an OpenGL context (texture upload).
 */
#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* ==========================================================================
 * Helpers: pixel access
 * ========================================================================== */

/*
 * get_pixel_rgba:
 *
 * Read the RGBA colour of a single pixel from @image at (@x, @y) into
 * @out_r/@out_g/@out_b/@out_a.  grl_image_get_pixel() returns a GrlColor
 * (boxed — freed with grl_color_free).
 */
static void
get_pixel_rgba (GrlImage *image,
                gint      x,
                gint      y,
                guint8   *out_r,
                guint8   *out_g,
                guint8   *out_b,
                guint8   *out_a)
{
    g_autoptr(GrlColor) c = NULL;

    c = grl_image_get_pixel (image, x, y);
    g_assert_nonnull (c);

    *out_r = c->r;
    *out_g = c->g;
    *out_b = c->b;
    *out_a = c->a;
}

/* ==========================================================================
 * Sample SVG data
 *
 * Simple 100×100 SVG with a red filled rectangle occupying [10,10,80,80].
 * ========================================================================== */

static const gchar SVG_RECT[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" "
    "     width=\"100\" height=\"100\">"
    "  <rect x=\"10\" y=\"10\" width=\"80\" height=\"80\" fill=\"#ff0000\"/>"
    "</svg>";

/* Malformed SVG strings used for error-path tests */
static const gchar SVG_GARBAGE[]  = "this is not svg at all!!!";
static const gchar SVG_NO_TAGS[]  = "   ";

/* ==========================================================================
 * Test: malformed SVG → NULL + GError
 * ========================================================================== */

static void
test_vector_image_malformed_garbage (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;

    vi = lrg_vector_image_new_from_data (SVG_GARBAGE,
                                         strlen (SVG_GARBAGE),
                                         &error);

    g_assert_null (vi);
    /* Either graylib or libregnum must set the error */
    g_assert_nonnull (error);
}

static void
test_vector_image_malformed_empty (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;

    vi = lrg_vector_image_new_from_data (SVG_NO_TAGS,
                                         strlen (SVG_NO_TAGS),
                                         &error);

    /*
     * Some SVG parsers return an empty shape array rather than an error for
     * whitespace-only input.  Accept either: NULL (error path) or a valid
     * object with 0 shapes that produces nothing useful.  The important
     * invariant is that the function does not crash.
     */
    if (vi == NULL)
    {
        g_assert_nonnull (error);
    }
    else
    {
        /* If it "succeeds", there must be no meaningful shapes */
        g_assert_cmpuint (lrg_vector_image_get_shape_count (vi), ==, 0);
    }
}

/* ==========================================================================
 * Test: valid SVG → non-NULL; get_shape_count; get_source_size
 * ========================================================================== */

static void
test_vector_image_load_valid (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;
    gboolean                  ok;
    gfloat                    w;
    gfloat                    h;

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);

    g_assert_no_error (error);
    g_assert_nonnull (vi);

    /* At least the rectangle shape must be present */
    g_assert_cmpuint (lrg_vector_image_get_shape_count (vi), >=, 1);

    /*
     * Source size: we use union bounds, so the result is the extent of the
     * filled rect (approximately 10..90 in each axis → width/height ~80).
     * Accept anything > 0.
     */
    ok = lrg_vector_image_get_source_size (vi, &w, &h);
    g_assert_true (ok);
    g_assert_cmpfloat (w, >, 0.0f);
    g_assert_cmpfloat (h, >, 0.0f);
}

/* ==========================================================================
 * Test: render → correct dimensions, interior pixel red, corner transparent
 * ========================================================================== */

static void
test_vector_image_render_transparent_bg (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;
    g_autoptr(GrlImage)       img   = NULL;
    guint8                    r;
    guint8                    g;
    guint8                    b;
    guint8                    a;

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);
    g_assert_no_error (error);
    g_assert_nonnull (vi);

    /* Render to 64×64 with no background (transparent) */
    img = lrg_vector_image_render (vi, 64, 64, NULL, FALSE);
    g_assert_nonnull (img);

    /* Verify dimensions */
    g_assert_cmpint (grl_image_get_width  (img), ==, 64);
    g_assert_cmpint (grl_image_get_height (img), ==, 64);

    /*
     * Interior pixel (centre, 32,32).  The rect covers roughly [10,10,90,90]
     * in the source 100×100 space, which after stretch-mapping to 64×64 puts
     * the rect at roughly [6,6,58,58].  The centre (32,32) is well inside.
     * Expect red-ish (r > 128, g < 64, b < 64) and fully opaque.
     */
    get_pixel_rgba (img, 32, 32, &r, &g, &b, &a);
    g_assert_cmpuint (r, >, 128);
    g_assert_cmpuint (g, <,  64);
    g_assert_cmpuint (b, <,  64);
    g_assert_cmpuint (a, >, 128);   /* opaque, painted by shape */

    /*
     * Corner pixel (0,0).  In the source SVG the rect starts at (10,10), so
     * after scaling to 64px the top-left corner should be outside the rect
     * and thus transparent (alpha == 0 with no background).
     * Allow a small alpha tolerance for antialiasing bleed (alpha < 32).
     */
    get_pixel_rgba (img, 0, 0, &r, &g, &b, &a);
    g_assert_cmpuint (a, <, 32);
}

/* ==========================================================================
 * Test: render with opaque white background → corner pixel == white
 * ========================================================================== */

static void
test_vector_image_render_white_bg (void)
{
    g_autoptr(LrgVectorImage) vi     = NULL;
    g_autoptr(GError)         error  = NULL;
    g_autoptr(GrlImage)       img    = NULL;
    g_autoptr(GrlColor)       white  = NULL;
    guint8                    r;
    guint8                    g;
    guint8                    b;
    guint8                    a;

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);
    g_assert_no_error (error);
    g_assert_nonnull (vi);

    white = grl_color_new (255, 255, 255, 255);

    img = lrg_vector_image_render (vi, 64, 64, white, FALSE);
    g_assert_nonnull (img);

    /*
     * The corner (0,0) is outside the red rect and should be the background
     * colour (white).
     */
    get_pixel_rgba (img, 0, 0, &r, &g, &b, &a);
    g_assert_cmpuint (r, >, 200);
    g_assert_cmpuint (g, >, 200);
    g_assert_cmpuint (b, >, 200);
    g_assert_cmpuint (a, ==, 255);
}

/* ==========================================================================
 * Test: render_to_texture → non-NULL (requires display)
 * ========================================================================== */

static void
test_vector_image_render_to_texture (void)
{
    g_autoptr(LrgVectorImage) vi      = NULL;
    g_autoptr(GError)         error   = NULL;
    g_autoptr(GrlTexture)     texture = NULL;

    SKIP_IF_NO_GRAPHICS ();

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);
    g_assert_no_error (error);
    g_assert_nonnull (vi);

    texture = lrg_vector_image_render_to_texture (vi, 64, 64, NULL, FALSE);
    g_assert_nonnull (texture);
}

/* ==========================================================================
 * Test: width/height <= 0 → NULL
 * ========================================================================== */

static void
test_vector_image_render_invalid_dimensions (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;
    g_autoptr(GrlImage)       img   = NULL;

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);
    g_assert_no_error (error);
    g_assert_nonnull (vi);

    img = lrg_vector_image_render (vi, 0, 64, NULL, FALSE);
    g_assert_null (img);

    img = lrg_vector_image_render (vi, 64, 0, NULL, FALSE);
    g_assert_null (img);

    img = lrg_vector_image_render (vi, -1, -1, NULL, FALSE);
    g_assert_null (img);

    {
        g_autoptr(GrlTexture) tex = lrg_vector_image_render_to_texture (vi, 0, 64, NULL, FALSE);
        g_assert_null (tex);
    }
}

/* ==========================================================================
 * Test: preserve_aspect → pixel outside scaled content area stays bg
 *
 * Render a wide target (128×32) with preserve_aspect=TRUE.  The source SVG
 * is 100×100 (square-ish, union bounds roughly 80×80).  The uniform scale
 * fits the height (32px) so the content occupies a 32×32 area centred in
 * the 128×32 target.  Pixels in the left/right letterbox bands should be
 * transparent (no bg).
 * ========================================================================== */

static void
test_vector_image_render_preserve_aspect_letterbox (void)
{
    g_autoptr(LrgVectorImage) vi    = NULL;
    g_autoptr(GError)         error = NULL;
    g_autoptr(GrlImage)       img   = NULL;
    gint                      iw;
    gint                      ih;
    guint8                    r;
    guint8                    g;
    guint8                    b;
    guint8                    a;

    vi = lrg_vector_image_new_from_data (SVG_RECT,
                                         strlen (SVG_RECT),
                                         &error);
    g_assert_no_error (error);
    g_assert_nonnull (vi);

    /*
     * Wide target: 128px wide, 32px tall with no background.
     * The uniform scale will be driven by the height (scale ~ 32/80 = 0.4),
     * so the artwork occupies approximately 32×32 pixels centred in the
     * 128-wide strip.  The leftmost column (x=0) is well outside the
     * centred content and should be transparent.
     */
    img = lrg_vector_image_render (vi, 128, 32, NULL, TRUE);
    g_assert_nonnull (img);

    iw = grl_image_get_width  (img);
    ih = grl_image_get_height (img);
    g_assert_cmpint (iw, ==, 128);
    g_assert_cmpint (ih, ==,  32);

    /*
     * Check a pixel in the left letterbox band.  x=2, y=16 is well within
     * the letterbox region (content is centred around x≈48..80).
     * We expect this to be transparent (alpha < 32).
     *
     * Note: this assertion is intentionally loose because the exact
     * letterbox boundary depends on the union bounds reported by graylib
     * for the particular SVG.  Any very-small alpha confirms no artwork was
     * drawn there.
     */
    get_pixel_rgba (img, 2, 16, &r, &g, &b, &a);
    g_assert_cmpuint (a, <, 32);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    int result;

    g_test_init (&argc, &argv, NULL);
    graphics_available = init_graphics_context ();

    /* Error paths */
    g_test_add_func ("/vector-image/malformed/garbage",
                     test_vector_image_malformed_garbage);
    g_test_add_func ("/vector-image/malformed/empty",
                     test_vector_image_malformed_empty);

    /* Loading */
    g_test_add_func ("/vector-image/load/valid",
                     test_vector_image_load_valid);

    /* Rendering */
    g_test_add_func ("/vector-image/render/transparent-bg",
                     test_vector_image_render_transparent_bg);
    g_test_add_func ("/vector-image/render/white-bg",
                     test_vector_image_render_white_bg);
    g_test_add_func ("/vector-image/render/to-texture",
                     test_vector_image_render_to_texture);
    g_test_add_func ("/vector-image/render/invalid-dimensions",
                     test_vector_image_render_invalid_dimensions);
    g_test_add_func ("/vector-image/render/preserve-aspect-letterbox",
                     test_vector_image_render_preserve_aspect_letterbox);

    result = g_test_run ();
    cleanup_graphics_context ();
    return result;
}
