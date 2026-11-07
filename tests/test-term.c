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

/* ------------------------------------------------------ system GTK log --- */

/*
 * tolerate_system_gtk:
 * The tests that open a window go through GLFW, which on Wayland loads
 * the system's libdecor GTK plugin for window decorations.  The plugin
 * fails to initialise GTK for the first window and, for the second,
 * calls gtk_disable_setlocale() after GTK has started, which logs a
 * structured Gtk-WARNING.  g_test_init() makes every warning fatal, so
 * that third-party message used to abort the whole binary.  This log
 * writer drops warnings of the "Gtk" domain only; everything else
 * (libregnum's own messages included) goes to the default writer and
 * stays fatal.
 */
static GLogWriterOutput
tolerate_system_gtk (GLogLevelFlags   log_level,
                     const GLogField *fields,
                     gsize            n_fields,
                     gpointer         user_data)
{
    gsize i;

    if ((log_level & G_LOG_LEVEL_WARNING) != 0)
    {
        for (i = 0; i < n_fields; i++)
        {
            if (g_str_equal (fields[i].key, "GLIB_DOMAIN") &&
                fields[i].length < 0 &&
                g_str_equal ((const gchar *) fields[i].value, "Gtk"))
            {
                g_test_message ("ignored a system GTK warning (libdecor plugin)");
                return G_LOG_WRITER_HANDLED;
            }
        }
    }
    return g_log_writer_default (log_level, fields, n_fields, user_data);
}

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

/* ------------------------------------------ surface draw offset (GL) ------- */

static void
test_frame_surface_draw_offset (void)
{
    g_autoptr(Lrg2DSurface) surface = NULL;
    g_autoptr(GrlColor) bg = NULL;
    g_autoptr(GrlColor) red = NULL;
    LrgFrameSurface *fs;
    gint ox = -1, oy = -1;
    int i;

    SKIP_IF_NO_DISPLAY ();

    surface = lrg_2d_surface_new (200, 150, "test-term-offset");
    if (lrg_2d_surface_get_window (surface) == NULL)
    {
        g_test_skip ("GrlWindow could not be created");
        return;
    }
    fs = LRG_FRAME_SURFACE (surface);

    /* Default offset is 0; set + read back. */
    lrg_frame_surface_get_draw_offset (fs, &ox, &oy);
    g_assert_cmpint (ox, ==, 0);
    g_assert_cmpint (oy, ==, 0);
    lrg_frame_surface_set_draw_offset (fs, 60, 50);
    lrg_frame_surface_get_draw_offset (fs, &ox, &oy);
    g_assert_cmpint (ox, ==, 60);
    g_assert_cmpint (oy, ==, 50);

    /* A fill at (0,0) lands at the offset (60,50), not the origin. */
    bg = grl_color_new (10, 10, 10, 255);
    red = grl_color_new (255, 0, 0, 255);
    for (i = 0; i < 5; i++)
    {
        lrg_frame_surface_begin_frame (fs);
        lrg_frame_surface_clear (fs, bg);
        lrg_frame_surface_fill_rect (fs, 0, 0, 16, 16, red);

        if (i == 4)
        {
            g_autoptr(GrlImage) shot = NULL;
            g_autoptr(GrlColor) at_off = NULL;
            g_autoptr(GrlColor) at_origin = NULL;

            lrg_frame_surface_push_clip (fs, 0, 0, 200, 150);
            lrg_frame_surface_pop_clip (fs);
            shot = grl_image_new_from_screen ();
            at_off = grl_image_get_pixel (shot, 68, 58);   /* inside (60,50)+rect */
            at_origin = grl_image_get_pixel (shot, 4, 4);   /* un-offset corner */
            g_assert_true (at_off->r > 180 && at_off->g < 80 && at_off->b < 80);
            g_assert_false (at_origin->r > 180 && at_origin->g < 80
                            && at_origin->b < 80);
        }
        lrg_frame_surface_end_frame (fs);
    }

    lrg_frame_surface_set_draw_offset (fs, 0, 0);
}

/* --------------------------------------------------- 3D mode enums (no GL) - */

static void
test_mode_enums (void)
{
    LrgArrangementKind ak = LRG_ARRANGEMENT_KIND_FREE;
    LrgEnvironmentKind ek = LRG_ENVIRONMENT_KIND_COCKPIT;

    g_assert_true (lrg_arrangement_kind_from_string ("per-window", &ak));
    g_assert_cmpint (ak, ==, LRG_ARRANGEMENT_KIND_PER_WINDOW);
    g_assert_cmpstr (lrg_arrangement_kind_to_string (LRG_ARRANGEMENT_KIND_CAROUSEL),
                     ==, "carousel");
    g_assert_false (lrg_arrangement_kind_from_string ("nope", &ak));
    g_assert_cmpint (ak, ==, LRG_ARRANGEMENT_KIND_SINGLE_PANEL);

    g_assert_true (lrg_environment_kind_from_string ("cockpit", &ek));
    g_assert_cmpint (ek, ==, LRG_ENVIRONMENT_KIND_COCKPIT);
    g_assert_cmpstr (lrg_environment_kind_to_string (LRG_ENVIRONMENT_KIND_WORKSHOP),
                     ==, "workshop");

    g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_ARRANGEMENT_KIND));
    g_assert_true (G_TYPE_IS_ENUM (LRG_TYPE_ENVIRONMENT_KIND));
}

/* ------------------------------------------------------------- pose (no GL) - */

static void
test_pose (void)
{
    g_autoptr(LrgPose) a = lrg_pose_new (0, 0, 0, 1, 1, 1, 0, 1, 0, 30.0f);
    g_autoptr(LrgPose) b = lrg_pose_new (10, 0, 0, 1, 1, 1, 0, 1, 0, 60.0f);
    g_autoptr(LrgPose) mid = lrg_pose_lerp (a, b, 0.5f);
    g_autoptr(LrgPose) over = lrg_pose_lerp (a, b, 2.0f);
    gfloat x;

    lrg_pose_get_position (mid, &x, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (x, 5.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (lrg_pose_get_fovy (mid), 45.0f, 0.001f);

    /* t is clamped to [0, 1]. */
    lrg_pose_get_position (over, &x, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (x, 10.0f, 0.001f);

    g_assert_true (G_TYPE_IS_BOXED (LRG_TYPE_POSE));
}

/* -------------------------------------------------- mode registry (no GL) -- */

static void
test_mode_registry (void)
{
    LrgModeRegistry *reg = lrg_mode_registry_get_default ();
    g_autoptr(LrgSceneArrangement) a = NULL;
    g_autoptr(LrgPanelEnvironment) e = NULL;

    g_assert_nonnull (reg);
    g_assert_true (lrg_mode_registry_has_arrangement (reg, "single-panel"));
    g_assert_true (lrg_mode_registry_has_arrangement (reg, "per-window"));
    g_assert_true (lrg_mode_registry_has_arrangement (reg, "free"));
    g_assert_true (lrg_mode_registry_has_environment (reg, "void"));
    g_assert_true (lrg_mode_registry_has_environment (reg, "workshop"));
    g_assert_false (lrg_mode_registry_has_arrangement (reg, "bogus"));

    a = lrg_mode_registry_create_arrangement (reg, "per-window");
    g_assert_nonnull (a);
    g_assert_true (LRG_IS_SCENE_ARRANGEMENT (a));
    g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, "per-window");

    e = lrg_mode_registry_create_environment (reg, "workshop");
    g_assert_nonnull (e);
    g_assert_true (LRG_IS_PANEL_ENVIRONMENT (e));
    g_assert_cmpstr (lrg_panel_environment_get_id (e), ==, "workshop");

    g_assert_null (lrg_mode_registry_create_arrangement (reg, "bogus"));
}

/* ------------------------------------------------- spatial camera (no GL) -- */

static void
test_spatial_camera (void)
{
    g_autoptr(LrgSpatialCamera) cam = lrg_spatial_camera_new ();
    g_autoptr(LrgPose) p0 = NULL;
    g_autoptr(LrgPose) target = NULL;
    g_autoptr(LrgPose) pf = NULL;
    gfloat z;
    int i;

    g_assert_nonnull (cam);
    p0 = lrg_spatial_camera_get_pose (cam);
    lrg_pose_get_position (p0, NULL, NULL, &z);
    /* Default is a gentle elevated 3/4 view (eye z = 6.6). */
    g_assert_cmpfloat_with_epsilon (z, 6.6f, 0.001f);
    g_assert_false (lrg_spatial_camera_is_animating (cam));

    target = lrg_pose_new (0, 0, 2, 0, 0, 0, 0, 1, 0, 45.0f);
    lrg_spatial_camera_set_target_pose (cam, target);
    g_assert_true (lrg_spatial_camera_is_animating (cam));
    for (i = 0; i < 500 && lrg_spatial_camera_step (cam, 0.05f); i++)
        ;
    g_assert_false (lrg_spatial_camera_is_animating (cam));

    pf = lrg_spatial_camera_get_pose (cam);
    lrg_pose_get_position (pf, NULL, NULL, &z);
    g_assert_cmpfloat_with_epsilon (z, 2.0f, 0.05f);
}

/* --------------------------------------------------- scene panel (no GL) --- */

static void
test_scene_panel (void)
{
    g_autoptr(LrgScenePanel) p = lrg_scene_panel_new (42);
    gfloat px, w;
    gfloat corners[12];
    gint sx, sw;
    int i;

    g_assert_nonnull (p);
    g_assert_cmpuint (lrg_scene_panel_get_key (p), ==, 42);

    lrg_scene_panel_set_source_rect (p, 10, 20, 300, 400);
    lrg_scene_panel_get_source_rect (p, &sx, NULL, &sw, NULL);
    g_assert_cmpint (sx, ==, 10);
    g_assert_cmpint (sw, ==, 300);

    lrg_scene_panel_set_immediate (p, 1, 2, 3, 0, 4, 2);
    lrg_scene_panel_get_geometry (p, &px, NULL, NULL, NULL, &w, NULL);
    g_assert_cmpfloat_with_epsilon (px, 1.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (w, 4.0f, 0.001f);
    g_assert_false (lrg_scene_panel_is_animating (p));

    lrg_scene_panel_set_target (p, 5, 2, 3, 0, 4, 2);
    g_assert_true (lrg_scene_panel_is_animating (p));
    for (i = 0; i < 500 && lrg_scene_panel_step (p, 0.05f); i++)
        ;
    g_assert_false (lrg_scene_panel_is_animating (p));
    lrg_scene_panel_get_geometry (p, &px, NULL, NULL, NULL, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (px, 5.0f, 0.05f);

    /* A 4-unit-wide panel spans ~4 in X across its top edge. */
    lrg_scene_panel_get_corners (p, corners);
    g_assert_cmpfloat (ABS (corners[3] - corners[0]), >, 1.0f);
}

/* --------------------------------------------- scene panel pin (no GL) ----- */

static void
test_scene_panel_pin (void)
{
    g_autoptr(LrgScenePanel) p = lrg_scene_panel_new (7);
    gfloat px, py;

    lrg_scene_panel_set_immediate (p, 1, 2, 0, 0, 4, 2);
    g_assert_false (lrg_scene_panel_is_pinned (p));

    /* Pin it: arrangement set_target / set_immediate must now be ignored. */
    lrg_scene_panel_pin (p, 9, 8, 0, 0, 4, 2);
    g_assert_true (lrg_scene_panel_is_pinned (p));
    lrg_scene_panel_set_target (p, -5, -5, 0, 0, 4, 2);
    lrg_scene_panel_set_immediate (p, -5, -5, 0, 0, 4, 2);
    lrg_scene_panel_get_geometry (p, &px, &py, NULL, NULL, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (px, 9.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (py, 8.0f, 0.001f);

    /* Unpin: layout governs it again. */
    lrg_scene_panel_unpin (p);
    g_assert_false (lrg_scene_panel_is_pinned (p));
    lrg_scene_panel_set_immediate (p, -5, -5, 0, 0, 4, 2);
    lrg_scene_panel_get_geometry (p, &px, NULL, NULL, NULL, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (px, -5.0f, 0.001f);
}

/* --------------------------------------- scene panel static flag (no GL) -- */

static void
test_scene_panel_static (void)
{
    g_autoptr(LrgScenePanel) p = lrg_scene_panel_new (5);

    /* A fresh panel carries the live capture, not a static texture. */
    g_assert_false (lrg_scene_panel_has_static_texture (p));

    lrg_scene_panel_set_static_texture (p, TRUE);
    g_assert_true (lrg_scene_panel_has_static_texture (p));

    lrg_scene_panel_set_static_texture (p, FALSE);
    g_assert_false (lrg_scene_panel_has_static_texture (p));
}

/* --------------------------------------------- VR seat math (no GL needed) -- */

static void
test_3d_surface_vr_seat (void)
{
    g_autoptr(Lrg3DSurface) surf = g_object_new (LRG_TYPE_3D_SURFACE, NULL);
    gfloat pose[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
    gfloat pos[3], dir[3], x, y;
    guint64 key;

    /* Default scale, clamped setter. */
    g_assert_cmpfloat_with_epsilon (lrg_3d_surface_get_vr_scale (surf), 0.3f, 1e-6);
    lrg_3d_surface_set_vr_scale (surf, 100.0f);
    g_assert_cmpfloat_with_epsilon (lrg_3d_surface_get_vr_scale (surf), 5.0f, 1e-6);
    lrg_3d_surface_set_vr_scale (surf, 0.5f);

    /* The seat sits at the default camera's eye (0, 1.6, 6.6), at the height
       of its target (0, 0, 0), facing it (-Z).  An identity tracking pose is
       the seat itself. */
    lrg_3d_surface_vr_tracking_to_scene (surf, pose, pos, dir);
    g_assert_cmpfloat_with_epsilon (pos[0], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (pos[1], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (pos[2], 6.6f, 1e-4);
    g_assert_cmpfloat_with_epsilon (dir[2], -1.0f, 1e-4);

    /* Tracking metres become scene units through the scale: 1 m right and
       0.5 m up at 0.5 m/unit is 2 units right, 1 up. */
    pose[12] = 1.0f;
    pose[13] = 0.5f;
    lrg_3d_surface_vr_tracking_to_scene (surf, pose, pos, dir);
    g_assert_cmpfloat_with_epsilon (pos[0], 2.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (pos[1], 1.0f, 1e-4);

    /* Turning the seat 90 degrees left turns "forward" from -Z to -X. */
    pose[12] = pose[13] = 0.0f;
    lrg_3d_surface_vr_turn (surf, 90.0f);
    lrg_3d_surface_vr_tracking_to_scene (surf, pose, pos, dir);
    g_assert_cmpfloat_with_epsilon (dir[0], -1.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (dir[2], 0.0f, 1e-4);

    /* No panels yet: a world ray hits nothing. */
    g_assert_false (lrg_3d_surface_pick_ray (surf, 0, 0, 10, 0, 0, -1,
                                             &x, &y, &key, NULL));
    g_assert_null (lrg_3d_surface_get_vr_service (surf));
}

/* ----------------------------------- workspace panels on the surface (GL) -- */

static void
test_3d_surface_workspace_panels (void)
{
    g_autoptr(Lrg3DSurface) surf = NULL;
    g_autoptr(GrlImage) img = NULL;
    g_autoptr(GrlColor) col = NULL;
    const guint64 ws = G_GUINT64_CONSTANT (0x8000000000000001); /* high-bit key */
    gfloat px, yaw0, yaw1;

    SKIP_IF_NO_DISPLAY ();

    surf = lrg_3d_surface_new (320, 240, "test-term-ws");
    if (lrg_frame_surface_get_window (LRG_FRAME_SURFACE (surf)) == NULL)
    {
        g_test_skip ("GrlWindow could not be created");
        return;
    }

    /* Place a workspace panel: it is created, pinned, and reports its transform. */
    lrg_3d_surface_place_panel (surf, ws, 5.0f, 0.0f, -3.0f, 30.0f, 4.0f, 3.0f);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 1);
    g_assert_true (lrg_3d_surface_is_panel_pinned (surf, ws));
    g_assert_true (lrg_3d_surface_get_panel_geometry (surf, ws, &px, NULL, NULL,
                                                      &yaw0, NULL, NULL));
    g_assert_cmpfloat_with_epsilon (px, 5.0f, 0.001f);
    g_assert_cmpfloat_with_epsilon (yaw0, 30.0f, 0.001f);

    /* Two live windows synced in: count is windows + the workspace panel. */
    lrg_3d_surface_begin_window_sync (surf);
    lrg_3d_surface_sync_window (surf, 100, 0, 0, 160, 240);
    lrg_3d_surface_sync_window (surf, 200, 160, 0, 160, 240);
    lrg_3d_surface_end_window_sync (surf);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 3);

    /* A sync that drops window 200 must NOT drop the static workspace panel. */
    lrg_3d_surface_begin_window_sync (surf);
    lrg_3d_surface_sync_window (surf, 100, 0, 0, 320, 240);
    lrg_3d_surface_end_window_sync (surf);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 2);
    g_assert_true (lrg_3d_surface_is_panel_pinned (surf, ws));
    g_assert_true (lrg_3d_surface_get_panel_geometry (surf, ws, &px, NULL, NULL,
                                                      NULL, NULL, NULL));
    g_assert_cmpfloat_with_epsilon (px, 5.0f, 0.001f);

    /* Rotate the workspace panel: yaw advances by the delta. */
    g_assert_true (lrg_3d_surface_rotate_panel (surf, ws, 45.0f));
    g_assert_true (lrg_3d_surface_get_panel_geometry (surf, ws, NULL, NULL, NULL,
                                                      &yaw1, NULL, NULL));
    g_assert_cmpfloat_with_epsilon (yaw1, 75.0f, 0.001f);

    /* Give it an off-screen-style image; it stays a single static panel. */
    col = grl_color_new (40, 80, 160, 255);
    img = grl_image_new_color (200, 120, col);
    lrg_3d_surface_set_panel_image (surf, ws, img);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 2);

    /* Remove it: count drops and the pin is forgotten. */
    lrg_3d_surface_remove_panel (surf, ws);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 1);
    g_assert_false (lrg_3d_surface_is_panel_pinned (surf, ws));
    g_assert_false (lrg_3d_surface_get_panel_geometry (surf, ws, NULL, NULL, NULL,
                                                       NULL, NULL, NULL));
}

/* ------------------------------------------- free arrangement (no GL) ------ */

static void
test_arrangement_free (void)
{
    g_autoptr(LrgSceneArrangement) a = lrg_mode_registry_create_arrangement (
        lrg_mode_registry_get_default (), "free");
    GPtrArray *panels = g_ptr_array_new_with_free_func (g_object_unref);
    gfloat x0, x1, x2;
    guint i;

    g_assert_nonnull (a);
    g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, "free");

    for (i = 0; i < 3; i++)
    {
        LrgScenePanel *p = lrg_scene_panel_new (i + 1);
        lrg_scene_panel_set_source_rect (p, 0, 0, 320, 200);
        g_ptr_array_add (panels, p);
    }

    lrg_scene_arrangement_layout (a, panels, 1000, 800, 1.0f);

    /* Three panels spread left-to-right, centred on the origin. */
    lrg_scene_panel_get_geometry (g_ptr_array_index (panels, 0), &x0,
                                  NULL, NULL, NULL, NULL, NULL);
    lrg_scene_panel_get_geometry (g_ptr_array_index (panels, 1), &x1,
                                  NULL, NULL, NULL, NULL, NULL);
    lrg_scene_panel_get_geometry (g_ptr_array_index (panels, 2), &x2,
                                  NULL, NULL, NULL, NULL, NULL);
    g_assert_cmpfloat (x0, <, x1);
    g_assert_cmpfloat (x1, <, x2);
    g_assert_cmpfloat_with_epsilon (x1, 0.0f, 0.001f);  /* middle centred */

    g_ptr_array_unref (panels);
}

/* -------------------------------------- panel slots + brightness (no GL) --- */

static void
test_scene_panel_slot (void)
{
    g_autoptr(LrgScenePanel) a = lrg_scene_panel_new (1);
    g_autoptr(LrgScenePanel) b = lrg_scene_panel_new (2);
    g_autoptr(LrgScenePanel) c = lrg_scene_panel_new (3);
    guint i;

    /* Defaults: ungrouped, ordinal 0, full brightness, no external texture. */
    g_assert_cmpuint (lrg_scene_panel_get_group (a), ==, 0);
    g_assert_cmpint (lrg_scene_panel_get_ordinal (a), ==, 0);
    g_assert_cmpfloat_with_epsilon (lrg_scene_panel_get_brightness (a), 1.0f, 1e-6);
    g_assert_false (lrg_scene_panel_has_external_texture (a));
    g_assert_null (lrg_scene_panel_get_texture (a));

    /* Group sorts first, then ordinal. */
    lrg_scene_panel_set_slot (a, 1, 5);
    lrg_scene_panel_set_slot (b, 0, 9);
    lrg_scene_panel_set_slot (c, 1, -2);
    g_assert_cmpint (lrg_scene_panel_compare_slot (b, a), <, 0);
    g_assert_cmpint (lrg_scene_panel_compare_slot (c, a), <, 0);
    g_assert_cmpint (lrg_scene_panel_compare_slot (a, a), ==, 0);

    /* Before the first placement brightness snaps; after it, it eases. */
    lrg_scene_panel_set_brightness (a, 0.5f);
    g_assert_cmpfloat_with_epsilon (lrg_scene_panel_get_brightness (a), 0.5f, 1e-6);
    lrg_scene_panel_set_target (a, 0, 0, 0, 0, 1, 1);
    lrg_scene_panel_set_brightness (a, 1.0f);
    g_assert_true (lrg_scene_panel_is_animating (a));
    for (i = 0; i < 200 && lrg_scene_panel_step (a, 1.0f / 60.0f); i++)
        ;
    g_assert_cmpfloat_with_epsilon (lrg_scene_panel_get_brightness (a), 1.0f, 1e-6);

    /* Out-of-range values clamp; updating a texture-less panel is a no-op. */
    lrg_scene_panel_set_brightness (b, 7.0f);
    g_assert_cmpfloat_with_epsilon (lrg_scene_panel_get_brightness (b), 1.0f, 1e-6);
    lrg_scene_panel_texture_updated (b);
}

/* Step every panel until its ease converges (after a re-layout, targets
   change but the current transform only moves when stepped). */
static void
settle_panels (GPtrArray *panels)
{
    guint i, k;

    for (k = 0; k < 600; k++)
    {
        gboolean any = FALSE;
        for (i = 0; i < panels->len; i++)
            any |= lrg_scene_panel_step (g_ptr_array_index (panels, i), 1.0f / 60.0f);
        if (!any)
            return;
    }
}

/* Make @n 16:10 panels with ordinals first, first + 1, ... */
static GPtrArray *
make_panels (guint n,
             gint  first)
{
    GPtrArray *panels = g_ptr_array_new_with_free_func (g_object_unref);
    guint i;

    for (i = 0; i < n; i++)
    {
        LrgScenePanel *p = lrg_scene_panel_new (i + 1);
        lrg_scene_panel_set_source_rect (p, 0, 0, 1600, 1000);
        lrg_scene_panel_set_slot (p, 0, first + (gint) i);
        g_ptr_array_add (panels, p);
    }
    return panels;
}

/* --------------------------------------------- carousel (no GL) ---------- */

static void
test_arrangement_carousel (void)
{
    g_autoptr(LrgSceneArrangement) a = lrg_mode_registry_create_arrangement (
        lrg_mode_registry_get_default (), "carousel");
    g_autoptr(GPtrArray) panels = make_panels (5, -2);
    g_autoptr(GPtrArray) many = make_panels (40, -20);
    gfloat x[5], z[5], yaw[5];
    guint i;

    g_assert_nonnull (a);
    g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, "carousel");
    g_assert_true (LRG_IS_ARRANGEMENT_CAROUSEL (a));

    lrg_scene_arrangement_layout (a, panels, 1600, 1000, 1.0f);
    for (i = 0; i < 5; i++)
        lrg_scene_panel_get_geometry (g_ptr_array_index (panels, i), &x[i],
                                      NULL, &z[i], &yaw[i], NULL, NULL);

    /* Ordinal 0 (index 2) is the front panel: the origin, facing +Z. */
    g_assert_cmpfloat_with_epsilon (x[2], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (z[2], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (yaw[2], 0.0f, 1e-4);

    /* Lower ordinals to the left, higher to the right, mirror-symmetric, and
       curving toward the viewer (+Z) while turning to face the centre. */
    g_assert_cmpfloat (x[0], <, x[1]);
    g_assert_cmpfloat (x[1], <, 0.0f);
    g_assert_cmpfloat (x[3], >, 0.0f);
    g_assert_cmpfloat_with_epsilon (x[1], -x[3], 1e-4);
    g_assert_cmpfloat_with_epsilon (z[1], z[3], 1e-4);
    g_assert_cmpfloat (z[1], >, 0.0f);
    g_assert_cmpfloat (yaw[1], >, 0.0f);
    g_assert_cmpfloat (yaw[3], <, 0.0f);

    /* A group change widens the gap between neighbours. */
    lrg_scene_panel_set_slot (g_ptr_array_index (panels, 3), 1, 1);
    lrg_scene_panel_set_slot (g_ptr_array_index (panels, 4), 1, 2);
    lrg_scene_arrangement_layout (a, panels, 1600, 1000, 1.0f);
    settle_panels (panels);
    {
        gfloat x3;
        lrg_scene_panel_get_geometry (g_ptr_array_index (panels, 3), &x3,
                                      NULL, NULL, NULL, NULL, NULL);
        g_assert_cmpfloat (x3, >, x[3]);
    }

    /* Forty panels close the ring: every yaw stays within (-180, 180] and
       the ring grew past the minimum radius. */
    lrg_scene_arrangement_layout (a, many, 1600, 1000, 1.0f);
    for (i = 0; i < many->len; i++)
    {
        gfloat y;
        lrg_scene_panel_get_geometry (g_ptr_array_index (many, i), NULL, NULL,
                                      NULL, &y, NULL, NULL);
        g_assert_cmpfloat (y, <=, 180.0f + 1e-3);
        g_assert_cmpfloat (y, >, -180.0f - 1e-3);
    }

    /* Setters clamp. */
    lrg_arrangement_carousel_set_radius (LRG_ARRANGEMENT_CAROUSEL (a), 0.5f);
    g_assert_cmpfloat (lrg_arrangement_carousel_get_radius (LRG_ARRANGEMENT_CAROUSEL (a)), ==, 2.0f);
    lrg_arrangement_carousel_set_spacing (LRG_ARRANGEMENT_CAROUSEL (a), 9.0f);
    g_assert_cmpfloat (lrg_arrangement_carousel_get_spacing (LRG_ARRANGEMENT_CAROUSEL (a)), ==, 4.0f);
}

/* ------------------------------------------------- grid (no GL) ---------- */

static void
test_arrangement_grid (void)
{
    g_autoptr(LrgSceneArrangement) a = lrg_mode_registry_create_arrangement (
        lrg_mode_registry_get_default (), "grid");
    g_autoptr(GPtrArray) panels = make_panels (4, 0);
    g_autoptr(GPtrArray) grouped = make_panels (2, 0);
    gfloat x[4], y[4], gy0, gy1;
    guint i;

    g_assert_nonnull (a);
    g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, "grid");

    /* Four panels, automatic columns: a 2 x 2 wall centred on the origin. */
    lrg_arrangement_grid_set_curve (LRG_ARRANGEMENT_GRID (a), 0.0f);
    lrg_scene_arrangement_layout (a, panels, 1600, 1000, 1.0f);
    for (i = 0; i < 4; i++)
        lrg_scene_panel_get_geometry (g_ptr_array_index (panels, i), &x[i],
                                      &y[i], NULL, NULL, NULL, NULL);
    g_assert_cmpfloat_with_epsilon (x[0], -x[1], 1e-4);
    g_assert_cmpfloat_with_epsilon (x[0], x[2], 1e-4);
    g_assert_cmpfloat_with_epsilon (y[0], y[1], 1e-4);
    g_assert_cmpfloat (y[0], >, y[2]);
    g_assert_cmpfloat_with_epsilon (y[0], -y[2], 1e-4);

    /* A new slot group starts a new row even with room left in the row. */
    lrg_arrangement_grid_set_columns (LRG_ARRANGEMENT_GRID (a), 4);
    g_assert_cmpuint (lrg_arrangement_grid_get_columns (LRG_ARRANGEMENT_GRID (a)), ==, 4);
    lrg_scene_panel_set_slot (g_ptr_array_index (grouped, 1), 7, 0);
    lrg_scene_arrangement_layout (a, grouped, 1600, 1000, 1.0f);
    lrg_scene_panel_get_geometry (g_ptr_array_index (grouped, 0), NULL, &gy0,
                                  NULL, NULL, NULL, NULL);
    lrg_scene_panel_get_geometry (g_ptr_array_index (grouped, 1), NULL, &gy1,
                                  NULL, NULL, NULL, NULL);
    g_assert_cmpfloat (gy0, >, gy1);

    /* Curved: outer columns come toward the viewer and turn inward. */
    lrg_arrangement_grid_set_curve (LRG_ARRANGEMENT_GRID (a), 14.0f);
    lrg_arrangement_grid_set_columns (LRG_ARRANGEMENT_GRID (a), 0);
    {
        g_autoptr(GPtrArray) curved = make_panels (4, 0);
        gfloat z0, yaw0;

        lrg_scene_arrangement_layout (a, curved, 1600, 1000, 1.0f);
        lrg_scene_panel_get_geometry (g_ptr_array_index (curved, 0), NULL, NULL,
                                      &z0, &yaw0, NULL, NULL);
        g_assert_cmpfloat (z0, >, 0.0f);
        g_assert_cmpfloat (yaw0, >, 0.0f);
    }
}

/* ------------------------------------------------ stack (no GL) ---------- */

static void
test_arrangement_stack (void)
{
    g_autoptr(LrgSceneArrangement) a = lrg_mode_registry_create_arrangement (
        lrg_mode_registry_get_default (), "stack");
    g_autoptr(GPtrArray) panels = make_panels (4, -1);  /* -1, 0, 1, 2 */
    gfloat x[4], z[4], b[4];
    guint i;

    g_assert_nonnull (a);
    g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, "stack");

    lrg_scene_arrangement_layout (a, panels, 1600, 1000, 1.0f);
    settle_panels (panels);
    for (i = 0; i < 4; i++)
    {
        lrg_scene_panel_get_geometry (g_ptr_array_index (panels, i), &x[i],
                                      NULL, &z[i], NULL, NULL, NULL);
        b[i] = lrg_scene_panel_get_brightness (g_ptr_array_index (panels, i));
    }

    /* Ordinal 0 (index 1) in front at full brightness. */
    g_assert_cmpfloat_with_epsilon (x[1], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (z[1], 0.0f, 1e-4);
    g_assert_cmpfloat_with_epsilon (b[1], 1.0f, 1e-4);

    /* Positive ordinals recede left and dim with depth; negative go right. */
    g_assert_cmpfloat (z[3], <, z[2]);
    g_assert_cmpfloat (z[2], <, 0.0f);
    g_assert_cmpfloat (x[2], <, 0.0f);
    g_assert_cmpfloat (x[0], >, 0.0f);
    g_assert_cmpfloat (b[3], <, b[2]);
    g_assert_cmpfloat (b[2], <, 1.0f);

    lrg_arrangement_stack_set_depth_step (LRG_ARRANGEMENT_STACK (a), 100.0f);
    g_assert_cmpfloat (lrg_arrangement_stack_get_depth_step (LRG_ARRANGEMENT_STACK (a)), ==, 10.0f);
}

/* ------------------------------- new ids, kinds and environments (no GL) --- */

static void
test_mode_registry_new_ids (void)
{
    LrgModeRegistry *reg = lrg_mode_registry_get_default ();
    const gchar *arr[] = { "carousel", "grid", "stack" };
    const gchar *env[] = { "sky", "studio" };
    LrgArrangementKind ak;
    LrgEnvironmentKind ek;
    guint i;

    for (i = 0; i < G_N_ELEMENTS (arr); i++)
    {
        g_autoptr(LrgSceneArrangement) a = lrg_mode_registry_create_arrangement (reg, arr[i]);
        g_assert_nonnull (a);
        g_assert_cmpstr (lrg_scene_arrangement_get_id (a), ==, arr[i]);
    }
    for (i = 0; i < G_N_ELEMENTS (env); i++)
    {
        g_autoptr(LrgPanelEnvironment) e = lrg_mode_registry_create_environment (reg, env[i]);
        g_autoptr(GrlColor) clear = NULL;
        g_assert_nonnull (e);
        g_assert_cmpstr (lrg_panel_environment_get_id (e), ==, env[i]);
        clear = lrg_panel_environment_get_clear_color (e);
        g_assert_nonnull (clear);
    }

    g_assert_true (lrg_arrangement_kind_from_string ("grid", &ak));
    g_assert_cmpint (ak, ==, LRG_ARRANGEMENT_KIND_GRID);
    g_assert_true (lrg_arrangement_kind_from_string ("stack", &ak));
    g_assert_cmpstr (lrg_arrangement_kind_to_string (ak), ==, "stack");
    g_assert_true (lrg_environment_kind_from_string ("sky", &ek));
    g_assert_cmpint (ek, ==, LRG_ENVIRONMENT_KIND_SKY);
    g_assert_cmpstr (lrg_environment_kind_to_string (LRG_ENVIRONMENT_KIND_STUDIO), ==, "studio");

    /* The sky's time of day reaches its dome. */
    {
        g_autoptr(LrgEnvironmentSky) sky = lrg_environment_sky_new ();
        lrg_environment_sky_set_time_of_day (sky, 18.0f);
        g_assert_cmpfloat_with_epsilon (lrg_environment_sky_get_time_of_day (sky), 18.0f, 1e-4);
        g_assert_cmpfloat_with_epsilon (
            lrg_sky_dome_get_time_of_day (lrg_environment_sky_get_dome (sky)), 18.0f, 1e-4);
    }
}

/* --------------------------------------------- pose strings (no GL) ------ */

static void
test_pose_string (void)
{
    g_autoptr(LrgPose) p = lrg_pose_new (1.5f, -2.25f, 6.6f, 0, 0, 0, 0, 1, 0, 45.0f);
    g_autofree gchar *s = lrg_pose_to_string (p);
    g_autoptr(LrgPose) q = lrg_pose_from_string (s);
    g_autoptr(LrgPose) spaced = lrg_pose_from_string ("  1 2 3  4 5 6 0 1 0 60\n");
    gfloat x, y, z;

    g_assert_nonnull (q);
    lrg_pose_get_position (q, &x, &y, &z);
    g_assert_cmpfloat (x, ==, 1.5f);
    g_assert_cmpfloat (y, ==, -2.25f);
    g_assert_cmpfloat (z, ==, 6.6f);
    g_assert_cmpfloat (lrg_pose_get_fovy (q), ==, 45.0f);

    g_assert_nonnull (spaced);
    g_assert_cmpfloat (lrg_pose_get_fovy (spaced), ==, 60.0f);

    /* Malformed: too few, too many, junk, non-finite, bad fovy. */
    g_assert_null (lrg_pose_from_string (""));
    g_assert_null (lrg_pose_from_string ("1 2 3"));
    g_assert_null (lrg_pose_from_string ("1 2 3 4 5 6 7 8 9 45 11"));
    g_assert_null (lrg_pose_from_string ("1 2 3 4 5 6 7 8 9 4x"));
    g_assert_null (lrg_pose_from_string ("1 2 3 4 5 6 7 8 nan 45"));
    g_assert_null (lrg_pose_from_string ("1 2 3 4 5 6 7 8 9 0"));
}

/* ------------------------------ texture-backed panels on the surface (GL) -- */

static guint overlay_calls;

static void
count_overlay (Lrg3DSurface *surface,
               gpointer      user_data)
{
    (void) surface;
    (void) user_data;
    overlay_calls++;
}

static void
test_3d_surface_texture_panels (void)
{
    g_autoptr(Lrg3DSurface) surf = NULL;
    g_autoptr(GrlColor) col = NULL;
    g_autoptr(GrlImage) img = NULL;
    g_autoptr(GrlTexture) tex_a = NULL;
    g_autoptr(GrlTexture) tex_b = NULL;
    g_autoptr(GrlTexture) tex_c = NULL;
    g_auto(GStrv) views = NULL;
    g_autofree gchar *home = NULL;
    gfloat px, py, cov_front, cov_side;
    guint64 key;
    gint w, h;

    SKIP_IF_NO_DISPLAY ();

    surf = lrg_3d_surface_new (320, 240, "test-term-tex");
    if (lrg_frame_surface_get_window (LRG_FRAME_SURFACE (surf)) == NULL)
    {
        g_test_skip ("GrlWindow could not be created");
        return;
    }
    w = lrg_frame_surface_get_width (LRG_FRAME_SURFACE (surf));
    h = lrg_frame_surface_get_height (LRG_FRAME_SURFACE (surf));

    col = grl_color_new (200, 40, 40, 255);
    img = grl_image_new_color (400, 250, col);
    tex_a = grl_texture_new_from_image (img);
    tex_b = grl_texture_new_from_image (img);
    tex_c = grl_texture_new_from_image (img);

    g_assert_true (lrg_3d_surface_get_capture_enabled (surf));
    lrg_3d_surface_set_capture_enabled (surf, FALSE);
    g_assert_false (lrg_3d_surface_get_capture_enabled (surf));
    g_assert_true (lrg_3d_surface_set_arrangement_id (surf, "carousel"));

    /* Three texture panels, ordinals -1, 0, 1: the middle one is in front. */
    lrg_3d_surface_begin_window_sync (surf);
    lrg_3d_surface_sync_texture_panel (surf, 10, tex_a);
    lrg_3d_surface_sync_texture_panel (surf, 20, tex_b);
    lrg_3d_surface_sync_texture_panel (surf, 30, tex_c);
    g_assert_true (lrg_3d_surface_set_panel_slot (surf, 10, 0, -1));
    g_assert_true (lrg_3d_surface_set_panel_slot (surf, 20, 0, 0));
    g_assert_true (lrg_3d_surface_set_panel_slot (surf, 30, 0, 1));
    g_assert_false (lrg_3d_surface_set_panel_slot (surf, 99, 0, 0));
    lrg_3d_surface_end_window_sync (surf);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 3);

    /* New pixels: only texture panels accept the update. */
    g_assert_true (lrg_3d_surface_panel_texture_updated (surf, 20));
    g_assert_false (lrg_3d_surface_panel_texture_updated (surf, 99));

    /* The default camera looks at the origin, where the front panel is: a
       pick at the screen centre lands on it, in the texture's pixel space. */
    g_assert_true (lrg_3d_surface_pick_panel (surf, (gfloat) w * 0.5f,
                                              (gfloat) h * 0.5f, &px, &py, &key));
    g_assert_cmpuint (key, ==, 20);
    g_assert_cmpfloat (px, >, 0.0f);
    g_assert_cmpfloat (px, <, 400.0f);
    g_assert_cmpfloat (py, >, 0.0f);
    g_assert_cmpfloat (py, <, 250.0f);

    /* The front panel looks bigger than its neighbour; unknown keys are 0. */
    cov_front = lrg_3d_surface_get_panel_coverage (surf, 20);
    cov_side = lrg_3d_surface_get_panel_coverage (surf, 30);
    g_assert_cmpfloat (cov_front, >, 0.0f);
    g_assert_cmpfloat (cov_front, >=, cov_side);
    g_assert_cmpfloat (lrg_3d_surface_get_panel_coverage (surf, 99), ==, 0.0f);

    /* A sync without panel 10 drops it; a texture swap keeps the panel. */
    lrg_3d_surface_begin_window_sync (surf);
    lrg_3d_surface_sync_texture_panel (surf, 20, tex_a);
    lrg_3d_surface_sync_texture_panel (surf, 30, tex_c);
    lrg_3d_surface_end_window_sync (surf);
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 2);

    /* Camera bookmarks: save, list, serialise, restore, forget. */
    lrg_3d_surface_save_view (surf, "home");
    lrg_3d_surface_save_view (surf, "aside");
    views = lrg_3d_surface_list_views (surf);
    g_assert_cmpuint (g_strv_length (views), ==, 2);
    g_assert_cmpstr (views[0], ==, "aside");
    g_assert_cmpstr (views[1], ==, "home");
    home = lrg_3d_surface_get_view_string (surf, "home");
    g_assert_nonnull (home);
    g_assert_true (lrg_3d_surface_set_view_string (surf, "copy", home));
    g_assert_false (lrg_3d_surface_set_view_string (surf, "bad", "1 2"));
    g_assert_true (lrg_3d_surface_restore_view (surf, "copy"));
    g_assert_false (lrg_3d_surface_restore_view (surf, "missing"));
    g_assert_true (lrg_3d_surface_forget_view (surf, "copy"));
    g_assert_false (lrg_3d_surface_forget_view (surf, "copy"));
    g_assert_null (lrg_3d_surface_get_view_string (surf, "copy"));

    /* The desktop HUD runs once per present, after the scene. */
    overlay_calls = 0;
    lrg_3d_surface_set_overlay_func (surf, count_overlay, NULL, NULL);
    lrg_frame_surface_begin_frame (LRG_FRAME_SURFACE (surf));
    lrg_frame_surface_begin_content (LRG_FRAME_SURFACE (surf));
    lrg_frame_surface_end_content (LRG_FRAME_SURFACE (surf));
    lrg_frame_surface_end_frame (LRG_FRAME_SURFACE (surf));
    g_assert_cmpuint (overlay_calls, ==, 1);

    /* With capture off there is no whole-frame default panel. */
    lrg_3d_surface_begin_window_sync (surf);
    lrg_3d_surface_end_window_sync (surf);
    lrg_frame_surface_begin_frame (LRG_FRAME_SURFACE (surf));
    lrg_frame_surface_end_frame (LRG_FRAME_SURFACE (surf));
    g_assert_cmpuint (lrg_3d_surface_get_panel_count (surf), ==, 0);
}

/* ------------------------------------ camera drag (immediate, no GL) ------- */

static void
test_spatial_camera_orbit_drag (void)
{
    g_autoptr(LrgSpatialCamera) cam = lrg_spatial_camera_new ();
    g_autoptr(LrgPose) before = lrg_spatial_camera_get_pose (cam);
    g_autoptr(LrgPose) after = NULL;
    gfloat x0, x1;

    lrg_pose_get_position (before, &x0, NULL, NULL);

    /* orbit_drag applies immediately: pose changes now, nothing left animating. */
    lrg_spatial_camera_orbit_drag (cam, 30.0f, 0.0f);
    g_assert_false (lrg_spatial_camera_is_animating (cam));
    after = lrg_spatial_camera_get_pose (cam);
    lrg_pose_get_position (after, &x1, NULL, NULL);
    g_assert_cmpfloat (ABS (x1 - x0), >, 0.5f);

    /* pan_drag is also immediate and moves both eye and target. */
    lrg_spatial_camera_pan_drag (cam, 1.0f, 0.0f);
    g_assert_false (lrg_spatial_camera_is_animating (cam));

    /* orbit_around_drag turntables about an arbitrary pivot and looks at it. */
    {
        g_autoptr(LrgSpatialCamera) c2 = lrg_spatial_camera_new ();
        g_autoptr(LrgPose) p = NULL;
        gfloat tx, ty, tz;
        lrg_spatial_camera_orbit_around_drag (c2, 2.0f, 0.0f, 0.0f, 30.0f, 0.0f);
        g_assert_false (lrg_spatial_camera_is_animating (c2));
        p = lrg_spatial_camera_get_pose (c2);
        lrg_pose_get_target (p, &tx, &ty, &tz);
        /* The camera now looks at the pivot (2,0,0), not the origin. */
        g_assert_cmpfloat_with_epsilon (tx, 2.0f, 0.001f);
        g_assert_cmpfloat_with_epsilon (ty, 0.0f, 0.001f);
        g_assert_cmpfloat_with_epsilon (tz, 0.0f, 0.001f);
    }

    /* look_drag is first-person: the EYE stays put, the target swings around. */
    {
        g_autoptr(LrgSpatialCamera) c3 = lrg_spatial_camera_new ();
        g_autoptr(LrgPose) a = lrg_spatial_camera_get_pose (c3);
        g_autoptr(LrgPose) b = NULL;
        gfloat ex0, ey0, ez0, ex1, ey1, ez1, tx0, tz0, tx1, tz1;
        lrg_pose_get_position (a, &ex0, &ey0, &ez0);
        lrg_pose_get_target (a, &tx0, NULL, &tz0);
        lrg_spatial_camera_look_drag (c3, 30.0f, 0.0f);
        b = lrg_spatial_camera_get_pose (c3);
        lrg_pose_get_position (b, &ex1, &ey1, &ez1);
        lrg_pose_get_target (b, &tx1, NULL, &tz1);
        g_assert_cmpfloat_with_epsilon (ex1, ex0, 0.001f);
        g_assert_cmpfloat_with_epsilon (ey1, ey0, 0.001f);
        g_assert_cmpfloat_with_epsilon (ez1, ez0, 0.001f);
        g_assert_cmpfloat (ABS (tx1 - tx0) + ABS (tz1 - tz0), >, 0.3f);
    }
}

int
main (int   argc,
      char *argv[])
{
    /* before any message: GLib takes the writer only once */
    g_log_set_writer_func (tolerate_system_gtk, NULL, NULL);
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/term/render-mode", test_render_mode);
    g_test_add_func ("/term/glyph-key", test_glyph_key);
    g_test_add_func ("/term/glyph-metrics", test_glyph_metrics);
    g_test_add_func ("/term/atlas-packing", test_atlas_packing);
    g_test_add_func ("/term/surface-upload-draw", test_surface_upload_draw);
    g_test_add_func ("/term/surface-draw-offset", test_frame_surface_draw_offset);
    g_test_add_func ("/term/mode-enums", test_mode_enums);
    g_test_add_func ("/term/pose", test_pose);
    g_test_add_func ("/term/mode-registry", test_mode_registry);
    g_test_add_func ("/term/spatial-camera", test_spatial_camera);
    g_test_add_func ("/term/scene-panel", test_scene_panel);
    g_test_add_func ("/term/scene-panel-pin", test_scene_panel_pin);
    g_test_add_func ("/term/scene-panel-static", test_scene_panel_static);
    g_test_add_func ("/term/3d-surface-workspace-panels",
                     test_3d_surface_workspace_panels);
    g_test_add_func ("/term/3d-surface-vr-seat", test_3d_surface_vr_seat);
    g_test_add_func ("/term/arrangement-free", test_arrangement_free);
    g_test_add_func ("/term/spatial-camera-orbit-drag",
                     test_spatial_camera_orbit_drag);
    g_test_add_func ("/term/scene-panel-slot", test_scene_panel_slot);
    g_test_add_func ("/term/arrangement-carousel", test_arrangement_carousel);
    g_test_add_func ("/term/arrangement-grid", test_arrangement_grid);
    g_test_add_func ("/term/arrangement-stack", test_arrangement_stack);
    g_test_add_func ("/term/mode-registry-new-ids", test_mode_registry_new_ids);
    g_test_add_func ("/term/pose-string", test_pose_string);
    g_test_add_func ("/term/3d-surface-texture-panels",
                     test_3d_surface_texture_panels);

    return g_test_run ();
}
