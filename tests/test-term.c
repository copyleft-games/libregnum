/* test-term.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tests for the terminal/display backend module (output_lrg): render-mode enum,
 * glyph key/metrics boxed types, the GL-free shelf packer, and (with a display)
 * the Lrg2DSurface upload/draw path.
 */

#include <libregnum.h>
#include <graylib.h>
#include <glib.h>

#define SKIP_IF_NO_DISPLAY() \
    do { \
        if (g_getenv ("DISPLAY") == NULL && g_getenv ("WAYLAND_DISPLAY") == NULL) \
        { \
            g_test_skip ("No display available (headless environment)"); \
            return; \
        } \
    } while (0)

/* ----------------------------------------------------------------- enum --- */

static void
test_render_mode (void)
{
    LrgRenderMode mode = LRG_RENDER_MODE_3D;

    g_assert_true (lrg_render_mode_from_string ("2d", &mode));
    g_assert_cmpint (mode, ==, LRG_RENDER_MODE_2D);
    g_assert_true (lrg_render_mode_from_string ("3d", &mode));
    g_assert_cmpint (mode, ==, LRG_RENDER_MODE_3D);
    g_assert_true (lrg_render_mode_from_string ("3dvr", &mode));
    g_assert_cmpint (mode, ==, LRG_RENDER_MODE_3DVR);

    /* Unknown / NULL fall back to 2d and report not-recognised. */
    g_assert_false (lrg_render_mode_from_string ("bogus", &mode));
    g_assert_cmpint (mode, ==, LRG_RENDER_MODE_2D);
    g_assert_false (lrg_render_mode_from_string (NULL, &mode));
    g_assert_cmpint (mode, ==, LRG_RENDER_MODE_2D);

    g_assert_cmpstr (lrg_render_mode_to_string (LRG_RENDER_MODE_2D), ==, "2d");
    g_assert_cmpstr (lrg_render_mode_to_string (LRG_RENDER_MODE_3D), ==, "3d");
    g_assert_cmpstr (lrg_render_mode_to_string (LRG_RENDER_MODE_3DVR), ==, "3dvr");

    /* The enum is a registered GType (introspectable). */
    g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_RENDER_MODE));
}

/* ------------------------------------------------------------ glyph key --- */

static void
test_glyph_key (void)
{
    g_autoptr(LrgGlyphKey) k1 = lrg_glyph_key_new (0xABCDu, 65, 0);
    g_autoptr(LrgGlyphKey) k2 = lrg_glyph_key_new (0xABCDu, 65, 0);
    g_autoptr(LrgGlyphKey) k3 = lrg_glyph_key_new (0xABCDu, 66, 0);
    g_autoptr(LrgGlyphKey) k4 = lrg_glyph_key_new (0x1234u, 65, 0);
    g_autoptr(LrgGlyphKey) kc = lrg_glyph_key_copy (k1);

    g_assert_cmpuint (lrg_glyph_key_get_font_id (k1), ==, 0xABCDu);
    g_assert_cmpuint (lrg_glyph_key_get_glyph_code (k1), ==, 65);

    g_assert_true (lrg_glyph_key_equal (k1, k2));
    g_assert_true (lrg_glyph_key_equal (k1, kc));
    g_assert_false (lrg_glyph_key_equal (k1, k3));
    g_assert_false (lrg_glyph_key_equal (k1, k4));

    g_assert_cmpuint (lrg_glyph_key_hash (k1), ==, lrg_glyph_key_hash (k2));
    /* Different keys SHOULD usually hash differently (not guaranteed, but our
     * mix makes these three distinct). */
    g_assert_cmpuint (lrg_glyph_key_hash (k1), !=, lrg_glyph_key_hash (k3));
}

/* -------------------------------------------------------- glyph metrics --- */

static void
test_glyph_metrics (void)
{
    g_autoptr(LrgGlyphMetrics) m =
        lrg_glyph_metrics_new (2, 5, 6, 7, 8, 1, 9, 11, TRUE);
    g_autoptr(LrgGlyphMetrics) c = NULL;
    gint x, y, w, h;
    gfloat u0, v0, u1, v1;

    g_assert_cmpuint (lrg_glyph_metrics_get_page (m), ==, 2);
    lrg_glyph_metrics_get_rect (m, &x, &y, &w, &h);
    g_assert_cmpint (x, ==, 5);
    g_assert_cmpint (y, ==, 6);
    g_assert_cmpint (w, ==, 7);
    g_assert_cmpint (h, ==, 8);
    g_assert_cmpint (lrg_glyph_metrics_get_bearing_x (m), ==, 1);
    g_assert_cmpint (lrg_glyph_metrics_get_bearing_y (m), ==, 9);
    g_assert_cmpint (lrg_glyph_metrics_get_advance (m), ==, 11);
    g_assert_true (lrg_glyph_metrics_get_is_color (m));

    lrg_glyph_metrics_set_uv (m, 0.1f, 0.2f, 0.3f, 0.4f);
    lrg_glyph_metrics_get_uv (m, &u0, &v0, &u1, &v1);
    g_assert_cmpfloat_with_epsilon (u0, 0.1f, 0.0001f);
    g_assert_cmpfloat_with_epsilon (v1, 0.4f, 0.0001f);

    c = lrg_glyph_metrics_copy (m);
    g_assert_cmpuint (lrg_glyph_metrics_get_page (c), ==, 2);
    g_assert_true (lrg_glyph_metrics_get_is_color (c));
}

/* --------------------------------------------------- atlas packer (no GL) -- */

static void
on_page_added (LrgGlyphAtlas *atlas,
               guint          page,
               gpointer       user_data)
{
    guint *count = user_data;

    (void) atlas;
    (void) page;
    (*count)++;
}

static void
test_atlas_packing (void)
{
    g_autoptr(LrgGlyphAtlas) atlas = lrg_glyph_atlas_new (64, 64);
    guint pages_added = 0;
    guint pg = 99;
    gint x = -1, y = -1;
    int i;

    g_assert_cmpint (lrg_glyph_atlas_get_page_width (atlas), ==, 64);
    g_assert_cmpint (lrg_glyph_atlas_get_page_height (atlas), ==, 64);

    g_signal_connect (atlas, "page-added", G_CALLBACK (on_page_added),
                      &pages_added);

    /* First 20x20 lands at the 1px margin on page 0. */
    g_assert_true (lrg_glyph_atlas_reserve (atlas, 20, 20, &pg, &x, &y));
    g_assert_cmpuint (pg, ==, 0);
    g_assert_cmpint (x, ==, 1);
    g_assert_cmpint (y, ==, 1);
    g_assert_cmpuint (pages_added, ==, 1);

    /* Second advances along the shelf: 1 + 20 + 1 = 22. */
    g_assert_true (lrg_glyph_atlas_reserve (atlas, 20, 20, &pg, &x, &y));
    g_assert_cmpuint (pg, ==, 0);
    g_assert_cmpint (x, ==, 22);
    g_assert_cmpint (y, ==, 1);

    /* Third: x = 43, still shelf 0. */
    g_assert_true (lrg_glyph_atlas_reserve (atlas, 20, 20, &pg, &x, &y));
    g_assert_cmpint (x, ==, 43);
    g_assert_cmpint (y, ==, 1);

    /* Fourth wraps to a new shelf: y = 1 + 20 + 1 = 22, x = 1. */
    g_assert_true (lrg_glyph_atlas_reserve (atlas, 20, 20, &pg, &x, &y));
    g_assert_cmpuint (pg, ==, 0);
    g_assert_cmpint (x, ==, 1);
    g_assert_cmpint (y, ==, 22);

    /* Keep reserving until the 64x64 page overflows onto a second page. */
    for (i = 0; i < 12; i++)
        lrg_glyph_atlas_reserve (atlas, 20, 20, &pg, &x, &y);
    g_assert_cmpuint (lrg_glyph_atlas_get_page_count (atlas), ==, 2);
    g_assert_cmpuint (pages_added, ==, 2);

    /* A zero-size (space) glyph always succeeds and consumes no shelf. */
    g_assert_true (lrg_glyph_atlas_reserve (atlas, 0, 0, &pg, &x, &y));

    /* A glyph larger than a whole page cannot be placed. */
    g_assert_false (lrg_glyph_atlas_reserve (atlas, 100, 100, &pg, &x, &y));
}

/* ----------------------------------------------- surface + upload (GL) ----- */

static void
test_surface_upload_draw (void)
{
    g_autoptr(Lrg2DSurface) surface = NULL;
    g_autoptr(LrgGlyphAtlas) atlas = NULL;
    g_autoptr(LrgGlyphKey) key = NULL;
    g_autoptr(GrlColor) bg = NULL;
    g_autoptr(GrlColor) red = NULL;
    LrgFrameSurface *fs;
    LrgGlyphMetrics *m;
    guint8 *pixels;
    int i;

    SKIP_IF_NO_DISPLAY ();

    surface = lrg_2d_surface_new (200, 150, "test-term");
    /* If raylib could not open a window (e.g. broken GL), skip rather than
     * fail -- the packer tests already cover the GL-free logic. */
    if (lrg_2d_surface_get_window (surface) == NULL)
    {
        g_test_skip ("GrlWindow could not be created");
        return;
    }

    fs = LRG_FRAME_SURFACE (surface);
    g_assert_cmpint (lrg_frame_surface_get_render_mode (fs), ==,
                     LRG_RENDER_MODE_2D);
    g_assert_cmpint (lrg_frame_surface_get_width (fs), >, 0);
    g_assert_true (LRG_IS_TEXT_RENDERER (surface));

    /* Upload an 8x8 opaque-white alpha-mask glyph. */
    atlas = lrg_glyph_atlas_new (256, 256);
    key = lrg_glyph_key_new (1, 65, 0);
    pixels = g_malloc0 (8 * 8 * 4);
    for (i = 0; i < 8 * 8; i++)
    {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 255;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = 255;
    }
    m = lrg_glyph_atlas_upload (atlas, key, pixels, 8, 8, 0, 8, 10, FALSE);
    g_assert_nonnull (m);
    g_assert_cmpuint (lrg_glyph_atlas_get_glyph_count (atlas), ==, 1);
    g_assert_true (lrg_glyph_atlas_lookup (atlas, key) == m);
    g_assert_nonnull (lrg_glyph_atlas_get_page_texture (atlas, 0));

    /* Uploading the same key again returns the cached metrics. */
    g_assert_true (lrg_glyph_atlas_upload (atlas, key, pixels, 8, 8,
                                           0, 8, 10, FALSE) == m);
    g_assert_cmpuint (lrg_glyph_atlas_get_glyph_count (atlas), ==, 1);

    /* Draw a few frames with the glyph tinted red, then read back. */
    bg = grl_color_new (10, 10, 10, 255);
    red = grl_color_new (255, 0, 0, 255);
    for (i = 0; i < 5; i++)
    {
        gboolean capture = (i == 4);

        lrg_frame_surface_begin_frame (fs);
        lrg_frame_surface_clear (fs, bg);
        lrg_frame_surface_fill_rect (fs, 5, 5, 20, 20, red);
        lrg_frame_surface_draw_glyph (fs, atlas, key, 60.0f, 60.0f, red);

        if (capture)
        {
            g_autoptr(GrlImage) shot = NULL;
            long red_px = 0;
            int sx, sy, cw, ch;

            /* Flush the batch (scissor begin/end) so the readback sees our
             * draws, then capture before end_frame swaps. */
            lrg_frame_surface_push_clip (fs, 0, 0, 200, 150);
            lrg_frame_surface_pop_clip (fs);
            shot = grl_image_new_from_screen ();
            cw = grl_image_get_width (shot);
            ch = grl_image_get_height (shot);
            for (sy = 0; sy < ch; sy += 2)
            {
                for (sx = 0; sx < cw; sx += 2)
                {
                    g_autoptr(GrlColor) p = grl_image_get_pixel (shot, sx, sy);
                    if (p->r > 180 && p->g < 80 && p->b < 80)
                        red_px++;
                }
            }
            /* The fill_rect alone is 20x20; with the glyph there is plenty. */
            g_assert_cmpint (red_px, >, 50);
        }

        lrg_frame_surface_end_frame (fs);
    }

    g_free (pixels);
    g_clear_object (&atlas);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/term/render-mode", test_render_mode);
    g_test_add_func ("/term/glyph-key", test_glyph_key);
    g_test_add_func ("/term/glyph-metrics", test_glyph_metrics);
    g_test_add_func ("/term/atlas-packing", test_atlas_packing);
    g_test_add_func ("/term/surface-upload-draw", test_surface_upload_draw);

    return g_test_run ();
}
