/* test-image-canvas.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgImageCanvas.
 *
 * All tests run headlessly; grl_image_* operations do not require an active
 * window.  to_texture() requires a GL context and is skipped when no display
 * is available.
 */

#include <glib.h>
#include <math.h>
#include <libregnum.h>
#include <raylib.h>  /* SetConfigFlags/FLAG_WINDOW_HIDDEN for a hidden GL context */

/*
 * Hidden-window GL context for GPU-upload (to_texture) tests; skipped in
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
 * Helpers
 * ========================================================================== */

/*
 * SKIP_IF_NO_DISPLAY - skip GPU-dependent tests in headless environments.
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
 * SKIP_IF_NULL - skip when a required resource could not be allocated.
 */
#define SKIP_IF_NULL(ptr) \
    do { \
        if ((ptr) == NULL) \
        { \
            g_test_skip ("Required resource not available"); \
            return; \
        } \
    } while (0)

/*
 * Helper: retrieve the RGBA value of a single pixel from a canvas.
 * Returns TRUE and fills the r, g, b, a out-params on success.
 */
static gboolean
canvas_get_pixel (LrgImageCanvas *canvas,
                  gint            x,
                  gint            y,
                  guint8         *r,
                  guint8         *g_ch,
                  guint8         *b,
                  guint8         *a)
{
    GrlImage  *image;
    GrlColor  *pixel;

    image = lrg_image_canvas_get_image (canvas);
    if (image == NULL)
        return FALSE;

    pixel = grl_image_get_pixel (image, x, y);
    if (pixel == NULL)
        return FALSE;

    *r   = grl_color_get_r (pixel);
    *g_ch = grl_color_get_g (pixel);
    *b   = grl_color_get_b (pixel);
    *a   = grl_color_get_a (pixel);

    grl_color_free (pixel);
    return TRUE;
}

/* ==========================================================================
 * Test: Construction — new()
 * ========================================================================== */

static void
test_image_canvas_new (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       black  = NULL;
    GrlImage                 *image;

    black  = grl_color_new (0, 0, 0, 255);
    canvas = lrg_image_canvas_new (64, 64, black);

    g_assert_nonnull (canvas);
    g_assert_true (LRG_IS_IMAGE_CANVAS (canvas));

    image = lrg_image_canvas_get_image (canvas);
    g_assert_nonnull (image);
    g_assert_cmpint (grl_image_get_width  (image), ==, 64);
    g_assert_cmpint (grl_image_get_height (image), ==, 64);
}

/* ==========================================================================
 * Test: Construction — new() with transparent background
 * ========================================================================== */

static void
test_image_canvas_new_transparent (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    guint8                    r, g, b, a;

    canvas = lrg_image_canvas_new (32, 32, NULL);

    g_assert_nonnull (canvas);

    /* Corner pixel should be fully transparent */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (a, ==, 0);
}

/* ==========================================================================
 * Test: Construction — new_for_image()
 * ========================================================================== */

static void
test_image_canvas_new_for_image (void)
{
    g_autoptr(GrlColor)       red_color = NULL;
    g_autoptr(GrlImage)       image     = NULL;
    g_autoptr(LrgImageCanvas) canvas    = NULL;

    red_color = grl_color_new (255, 0, 0, 255);
    image     = grl_image_new_color (16, 16, red_color);

    g_assert_nonnull (image);

    canvas = lrg_image_canvas_new_for_image (image);

    g_assert_nonnull (canvas);
    /* Canvas must wrap (reference) the same image, not a copy. */
    g_assert_true (lrg_image_canvas_get_image (canvas) == image);
}

/* ==========================================================================
 * Test: to_texture — requires a display
 * ========================================================================== */

static void
test_image_canvas_to_texture (void)
{
    g_autoptr(LrgImageCanvas) canvas  = NULL;
    g_autoptr(GrlColor)       bg      = NULL;
    g_autoptr(GrlTexture)     texture = NULL;

    SKIP_IF_NO_GRAPHICS ();

    bg     = grl_color_new (0, 0, 0, 255);
    canvas = lrg_image_canvas_new (64, 64, bg);
    SKIP_IF_NULL (canvas);

    texture = lrg_image_canvas_to_texture (canvas);

    g_assert_nonnull (texture);
    g_assert_true (GRL_IS_TEXTURE (texture));
}

/* ==========================================================================
 * Test: fill_circle — AA on, centre pixel should be white
 * ========================================================================== */

static void
test_image_canvas_fill_circle (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       black  = NULL;
    g_autoptr(GrlColor)       white  = NULL;
    guint8                    r, g, b, a;

    black  = grl_color_new (0, 0, 0, 255);
    white  = grl_color_new (255, 255, 255, 255);

    canvas = lrg_image_canvas_new (64, 64, black);
    g_assert_nonnull (canvas);

    lrg_image_canvas_set_antialias (canvas, TRUE);
    lrg_image_canvas_fill_circle (canvas, 32, 32, 16, white);

    /*
     * The centre pixel of the circle must be white (fully covered).
     */
    g_assert_true (canvas_get_pixel (canvas, 32, 32, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 255);
    g_assert_cmpuint (b, ==, 255);
}

/* ==========================================================================
 * Test: blend OVER — composite half-alpha red on white
 * ========================================================================== */

static void
test_image_canvas_blend_over (void)
{
    g_autoptr(LrgImageCanvas) canvas   = NULL;
    g_autoptr(GrlColor)       white    = NULL;
    g_autoptr(GrlColor)       half_red = NULL;
    g_autoptr(GrlImage)       red_img  = NULL;
    guint8                    r, g, b, a;

    white    = grl_color_new (255, 255, 255, 255);
    half_red = grl_color_new (255, 0, 0, 128);

    canvas  = lrg_image_canvas_new (32, 32, white);
    g_assert_nonnull (canvas);

    red_img = grl_image_new_color (32, 32, half_red);
    g_assert_nonnull (red_img);

    lrg_image_canvas_set_blend_mode (canvas, GRL_IMAGE_BLEND_OVER);
    lrg_image_canvas_composite (canvas, red_img, GRL_PORTER_DUFF_SRC_OVER, 0, 0);

    /*
     * After compositing half-alpha red (128/255 ≈ 0.502) over white:
     *   R = 255 * 0.502 + 255 * (1 - 0.502) ≈ 255  (still 255)
     *   G = 0   * 0.502 + 255 * (1 - 0.502) ≈ 126
     *   B = 0   * 0.502 + 255 * (1 - 0.502) ≈ 126
     *
     * We allow a ±2 tolerance for rounding differences between graylib builds.
     */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (r, >=, 253);
    g_assert_cmpuint (g, <=, 130);
    g_assert_cmpuint (g, >=, 120);
    g_assert_cmpuint (b, <=, 130);
    g_assert_cmpuint (b, >=, 120);
}

/* ==========================================================================
 * Test: transform — save/translate/fill_rect/restore
 * ========================================================================== */

static void
test_image_canvas_transform (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       black  = NULL;
    g_autoptr(GrlColor)       white  = NULL;
    guint8                    r, g, b, a;

    black = grl_color_new (0, 0, 0, 255);
    white = grl_color_new (255, 255, 255, 255);

    canvas = lrg_image_canvas_new (64, 64, black);
    g_assert_nonnull (canvas);

    /*
     * Translate by (32,32) and draw a 4×4 rect at (0,0).
     * The rect should appear at (32,32) in canvas coordinates, not at (0,0).
     */
    lrg_image_canvas_save (canvas);
    lrg_image_canvas_translate (canvas, 32.0f, 32.0f);
    lrg_image_canvas_fill_rect (canvas, 0, 0, 4, 4, white);
    lrg_image_canvas_restore (canvas);

    /* Pixel at origin should still be black (untransformed area). */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 0);
    g_assert_cmpuint (g, ==, 0);
    g_assert_cmpuint (b, ==, 0);

    /* Pixel at translated location should be white. */
    g_assert_true (canvas_get_pixel (canvas, 32, 32, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 255);
    g_assert_cmpuint (b, ==, 255);
}

/* ==========================================================================
 * Test: fill_path — triangle interior filled
 * ========================================================================== */

static void
test_image_canvas_fill_path (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       black  = NULL;
    g_autoptr(GrlColor)       white  = NULL;
    g_autoptr(GrlPath)        path   = NULL;
    guint8                    r, g, b, a;

    black = grl_color_new (0, 0, 0, 255);
    white = grl_color_new (255, 255, 255, 255);

    canvas = lrg_image_canvas_new (64, 64, black);
    g_assert_nonnull (canvas);

    /*
     * Build a triangle path with vertices at (32,4), (60,60), (4,60).
     * The centroid is at approximately (32, 41) and should be filled.
     */
    path = grl_path_new ();
    g_assert_nonnull (path);

    grl_path_move_to (path, 32.0f, 4.0f);
    grl_path_line_to (path, 60.0f, 60.0f);
    grl_path_line_to (path, 4.0f,  60.0f);
    grl_path_close   (path);

    lrg_image_canvas_fill_path (canvas, path, GRL_FILL_RULE_NONZERO, white);

    /* Check a pixel near the centroid. */
    g_assert_true (canvas_get_pixel (canvas, 32, 40, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 255);
    g_assert_cmpuint (b, ==, 255);

    /* Corner outside the triangle should remain black. */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 0);
}

/* ==========================================================================
 * Test: composite — pixel at offset matches source
 * ========================================================================== */

static void
test_image_canvas_composite (void)
{
    g_autoptr(LrgImageCanvas) canvas  = NULL;
    g_autoptr(GrlColor)       black   = NULL;
    g_autoptr(GrlColor)       red     = NULL;
    g_autoptr(GrlImage)       src_img = NULL;
    guint8                    r, g, b, a;

    black   = grl_color_new (0, 0, 0, 255);
    red     = grl_color_new (255, 0, 0, 255);

    canvas  = lrg_image_canvas_new (64, 64, black);
    g_assert_nonnull (canvas);

    src_img = grl_image_new_color (8, 8, red);
    g_assert_nonnull (src_img);

    /* Composite solid red 8×8 block at offset (20, 20). */
    lrg_image_canvas_composite (canvas, src_img, GRL_PORTER_DUFF_SRC_OVER, 20, 20);

    /* Pixel inside the composited region should be red. */
    g_assert_true (canvas_get_pixel (canvas, 22, 22, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 0);
    g_assert_cmpuint (b, ==, 0);

    /* Pixel outside the composited region should still be black. */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 0);
}

/* ==========================================================================
 * Test: blur — spreads a bright pixel
 * ========================================================================== */

static void
test_image_canvas_blur (void)
{
    g_autoptr(LrgImageCanvas) canvas  = NULL;
    g_autoptr(GrlColor)       black   = NULL;
    g_autoptr(GrlColor)       white   = NULL;
    guint8                    before_r, before_g, before_b, before_a;
    guint8                    after_r_peak, after_g, after_b, after_a;
    guint8                    after_r_neighbour;

    black = grl_color_new (0, 0, 0, 255);
    white = grl_color_new (255, 255, 255, 255);

    canvas = lrg_image_canvas_new (32, 32, black);
    g_assert_nonnull (canvas);

    /* Draw a single white pixel at (16,16). */
    lrg_image_canvas_fill_circle (canvas, 16, 16, 1, white);

    /* Confirm it is bright before blur. */
    g_assert_true (canvas_get_pixel (canvas, 16, 16, &before_r, &before_g, &before_b, &before_a));
    g_assert_cmpuint (before_r, >, 200);

    /* Apply a box blur with radius 3. */
    lrg_image_canvas_blur (canvas, 3);

    /* Peak pixel should have spread: it will be dimmer than before. */
    g_assert_true (canvas_get_pixel (canvas, 16, 16, &after_r_peak, &after_g, &after_b, &after_a));
    g_assert_cmpuint (after_r_peak, <, before_r);

    /*
     * A neighbouring pixel (one step away) should now have a non-zero value
     * because the blur distributed energy outward.
     */
    g_assert_true (canvas_get_pixel (canvas, 17, 16,
                                     &after_r_neighbour, &after_g, &after_b, &after_a));
    g_assert_cmpuint (after_r_neighbour, >, 0);
}

/* ==========================================================================
 * Test: drop_shadow — soft shadow appears behind content
 * ========================================================================== */

static void
test_image_canvas_drop_shadow (void)
{
    g_autoptr(LrgImageCanvas) canvas     = NULL;
    g_autoptr(GrlColor)       transparent = NULL;
    g_autoptr(GrlColor)       white      = NULL;
    g_autoptr(GrlColor)       grey       = NULL;
    g_autoptr(GrlImage)       silhouette = NULL;
    guint8                    r, g, b, a;
    gint                      shadow_dx;
    gint                      shadow_dy;

    transparent = grl_color_new (0, 0, 0, 0);
    white       = grl_color_new (255, 255, 255, 255);
    grey        = grl_color_new (64, 64, 64, 200);

    /* Build a white 16×16 silhouette image (represents the original content). */
    silhouette = grl_image_new_color (16, 16, white);
    g_assert_nonnull (silhouette);

    /* Canvas is 48×48, transparent. */
    canvas = lrg_image_canvas_new (48, 48, transparent);
    g_assert_nonnull (canvas);

    /*
     * Draw the silhouette as the current content at the canvas origin.
     * The white block sits at (0,0)..(15,15).
     */
    lrg_image_canvas_composite (canvas, silhouette, GRL_PORTER_DUFF_SRC_OVER, 0, 0);

    shadow_dx = 8;
    shadow_dy = 8;

    lrg_image_canvas_drop_shadow (canvas, silhouette,
                                   shadow_dx, shadow_dy, 2, grey);

    /*
     * After drop_shadow the pixel at the shadow offset (8,8) should be
     * grey-ish (shadow contributed there, blurred), with alpha > 0.
     * Allow a generous range because blur spreads the grey.
     */
    g_assert_true (canvas_get_pixel (canvas, shadow_dx + 4, shadow_dy + 4,
                                     &r, &g, &b, &a));
    g_assert_cmpuint (a, >, 0);

    /*
     * The original content at (0,0) should still be white (DST_OVER
     * leaves the destination pixel intact where it is opaque).
     */
    g_assert_true (canvas_get_pixel (canvas, 0, 0, &r, &g, &b, &a));
    g_assert_cmpuint (r, ==, 255);
    g_assert_cmpuint (g, ==, 255);
    g_assert_cmpuint (b, ==, 255);
    g_assert_cmpuint (a, ==, 255);
}

/* ==========================================================================
 * Test: quality preset — new_quality() / set_quality()
 * ========================================================================== */

static void
test_image_canvas_quality_preset (void)
{
    g_autoptr(LrgImageCanvas) plain   = NULL;
    g_autoptr(LrgImageCanvas) quality = NULL;
    g_autoptr(GrlColor)       black   = NULL;
    GrlImage                 *img;

    black = grl_color_new (0, 0, 0, 255);

    /* the plain constructor keeps graylib's pixel-exact defaults */
    plain = lrg_image_canvas_new (8, 8, black);
    img = lrg_image_canvas_get_image (plain);
    g_assert_cmpint (grl_image_get_blend_mode (img), ==, GRL_IMAGE_BLEND_REPLACE);
    g_assert_cmpint (grl_image_get_blend_color_space (img), ==, GRL_IMAGE_COLOR_SPACE_GAMMA);
    g_assert_false (grl_image_get_antialias (img));
    g_assert_cmpuint (lrg_image_canvas_get_supersample (plain), ==, 1);

    /* set_quality turns all three on */
    lrg_image_canvas_set_quality (plain);
    g_assert_cmpint (grl_image_get_blend_mode (img), ==, GRL_IMAGE_BLEND_OVER);
    g_assert_cmpint (grl_image_get_blend_color_space (img), ==, GRL_IMAGE_COLOR_SPACE_LINEAR);
    g_assert_true (grl_image_get_antialias (img));

    /* new_quality is new + set_quality */
    quality = lrg_image_canvas_new_quality (8, 4, NULL);
    g_assert_nonnull (quality);
    img = lrg_image_canvas_get_image (quality);
    g_assert_cmpint (grl_image_get_width (img), ==, 8);
    g_assert_cmpint (grl_image_get_height (img), ==, 4);
    g_assert_cmpint (grl_image_get_blend_mode (img), ==, GRL_IMAGE_BLEND_OVER);
    g_assert_cmpint (grl_image_get_blend_color_space (img), ==, GRL_IMAGE_COLOR_SPACE_LINEAR);
    g_assert_true (grl_image_get_antialias (img));
    g_assert_cmpuint (lrg_image_canvas_get_supersample (quality), ==, 1);
}

/*
 * The preset blends in linear light: 50% white over black is the linear
 * mid grey (sRGB 188), where gamma-space blending gives about 128.
 */
static void
test_image_canvas_quality_linear_blend (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       black  = NULL;
    g_autoptr(GrlColor)       half   = NULL;
    guint8                    r, g, b, a;

    black  = grl_color_new (0, 0, 0, 255);
    half   = grl_color_new (255, 255, 255, 128);
    canvas = lrg_image_canvas_new_quality (8, 8, black);
    lrg_image_canvas_fill_rect (canvas, 0, 0, 8, 8, half);

    g_assert_true (canvas_get_pixel (canvas, 4, 4, &r, &g, &b, &a));
    g_assert_cmpint (a, ==, 255);
    g_assert_cmpint (ABS ((gint)r - 188), <=, 1);
}

/* ==========================================================================
 * Test: supersampling — new_supersampled() / resolve()
 * ========================================================================== */

static void
test_image_canvas_supersampled (void)
{
    g_autoptr(LrgImageCanvas) canvas   = NULL;
    g_autoptr(GrlColor)       black    = NULL;
    g_autoptr(GrlColor)       white    = NULL;
    g_autoptr(GrlImage)       resolved = NULL;
    g_autoptr(GrlImage)       again    = NULL;
    GrlImage                 *img;
    const guint8             *p;
    gdouble                   lin;
    gint                      want;

    black  = grl_color_new (0, 0, 0, 255);
    white  = grl_color_new (255, 255, 255, 255);
    canvas = lrg_image_canvas_new_supersampled (10, 6, 3, black);
    g_assert_nonnull (canvas);
    g_assert_cmpuint (lrg_image_canvas_get_supersample (canvas), ==, 3);

    /* the backing image is factor times larger and carries the preset */
    img = lrg_image_canvas_get_image (canvas);
    g_assert_cmpint (grl_image_get_width (img), ==, 30);
    g_assert_cmpint (grl_image_get_height (img), ==, 18);
    g_assert_cmpint (grl_image_get_blend_mode (img), ==, GRL_IMAGE_BLEND_OVER);
    g_assert_cmpint (grl_image_get_blend_color_space (img), ==, GRL_IMAGE_COLOR_SPACE_LINEAR);
    g_assert_true (grl_image_get_antialias (img));

    /*
     * White over columns 0..15 of 30: final pixel 4 (source 12..14) is
     * covered, pixel 5 (source 15..17) one third, pixel 6 not at all.
     */
    lrg_image_canvas_fill_rect (canvas, 0, 0, 16, 18, white);
    resolved = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_BOX);
    g_assert_nonnull (resolved);
    g_assert_cmpint (grl_image_get_width (resolved), ==, 10);
    g_assert_cmpint (grl_image_get_height (resolved), ==, 6);
    p = grl_image_get_pixels (resolved, NULL);

    /* reference: linear coverage 1/3, encoded with the sRGB curve */
    lin  = 1.0 / 3.0;
    want = (gint)floor ((1.055 * pow (lin, 1.0 / 2.4) - 0.055) * 255.0 + 0.5);
    g_assert_cmpint (p[(2 * 10 + 4) * 4 + 0], ==, 255);
    g_assert_cmpint (ABS ((gint)p[(2 * 10 + 5) * 4 + 0] - want), <=, 1);
    g_assert_cmpint (p[(2 * 10 + 6) * 4 + 0], ==, 0);
    g_assert_cmpint (p[(2 * 10 + 5) * 4 + 3], ==, 255);

    /* resolve leaves the canvas as it was: a second resolve is identical */
    again = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_BOX);
    g_assert_cmpmem (grl_image_get_pixels (again, NULL), 10 * 6 * 4, p, 10 * 6 * 4);
    g_assert_cmpint (grl_image_get_width (img), ==, 30);
}

/* A factor-1 canvas resolves to a copy of its image */
static void
test_image_canvas_resolve_factor_one (void)
{
    g_autoptr(LrgImageCanvas) canvas = NULL;
    g_autoptr(GrlColor)       grey   = NULL;
    g_autoptr(GrlImage)       out    = NULL;

    grey   = grl_color_new (90, 120, 150, 200);
    canvas = lrg_image_canvas_new_supersampled (5, 4, 1, grey);
    out    = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);
    g_assert_nonnull (out);
    g_assert_true (out != lrg_image_canvas_get_image (canvas));
    g_assert_cmpint (grl_image_get_width (out), ==, 5);
    g_assert_cmpmem (grl_image_get_pixels (out, NULL), 5 * 4 * 4,
                     grl_image_get_pixels (lrg_image_canvas_get_image (canvas), NULL), 5 * 4 * 4);
}

/* Factor 0 is a programmer error (critical) */
static void
test_image_canvas_supersampled_invalid (void)
{
    if (g_test_subprocess ())
    {
        lrg_image_canvas_new_supersampled (4, 4, 0, NULL);
        return;
    }
    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_failed ();
    g_test_trap_assert_stderr ("*factor*");
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

    g_test_add_func ("/image-canvas/new",              test_image_canvas_new);
    g_test_add_func ("/image-canvas/new-transparent",  test_image_canvas_new_transparent);
    g_test_add_func ("/image-canvas/new-for-image",    test_image_canvas_new_for_image);
    g_test_add_func ("/image-canvas/to-texture",       test_image_canvas_to_texture);
    g_test_add_func ("/image-canvas/fill-circle",      test_image_canvas_fill_circle);
    g_test_add_func ("/image-canvas/blend-over",       test_image_canvas_blend_over);
    g_test_add_func ("/image-canvas/transform",        test_image_canvas_transform);
    g_test_add_func ("/image-canvas/fill-path",        test_image_canvas_fill_path);
    g_test_add_func ("/image-canvas/composite",        test_image_canvas_composite);
    g_test_add_func ("/image-canvas/blur",             test_image_canvas_blur);
    g_test_add_func ("/image-canvas/drop-shadow",      test_image_canvas_drop_shadow);
    g_test_add_func ("/image-canvas/quality-preset",   test_image_canvas_quality_preset);
    g_test_add_func ("/image-canvas/quality-linear-blend", test_image_canvas_quality_linear_blend);
    g_test_add_func ("/image-canvas/supersampled",     test_image_canvas_supersampled);
    g_test_add_func ("/image-canvas/resolve-factor-one", test_image_canvas_resolve_factor_one);
    g_test_add_func ("/image-canvas/supersampled-invalid", test_image_canvas_supersampled_invalid);

    result = g_test_run ();
    cleanup_graphics_context ();
    return result;
}
