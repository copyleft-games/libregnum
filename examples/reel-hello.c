/* reel-hello.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Minimal Reel example: a title fades and springs into view, rendered
 * head-less to an animated GIF.  Build, then run from the repo root:
 *
 *   LD_LIBRARY_PATH=build/release/lib ./build/release/examples/reel-hello
 *
 * It writes "reel-hello.gif" in the current directory.
 */

#include <libregnum.h>
#include <graylib.h>

static void
draw_title (LrgReelClip    *clip,
            LrgReelContext *ctx,
            LrgImageCanvas *canvas,
            gpointer        user_data)
{
    gint     frame = lrg_reel_context_get_frame (ctx);
    gint     width = lrg_reel_context_get_width (ctx);
    GrlColor bg = { 12, 14, 28, 255 };
    GrlColor fg = { 255, 255, 255, 255 };
    gdouble  opacity;
    gdouble  y;

    /* Background. */
    lrg_image_canvas_clear (canvas, &bg);

    /* Fade the text in over the first 20 frames. */
    opacity = lrg_reel_interpolate_clamped (frame, 0.0, 20.0, 0.0, 1.0,
                                            LRG_EASING_EASE_OUT_CUBIC);

    /* Spring the baseline up from below into place. */
    y = lrg_reel_spring (frame, 30.0, NULL, 220.0, 150.0);

    fg.a = (guint8) (opacity * 255.0);
    grl_image_draw_text_bitmap (lrg_image_canvas_get_image (canvas),
                                "HELLO REEL",
                                width / 2 - 110, (gint) y, 32, &fg);
}

int
main (void)
{
    g_autoptr(LrgReel)            reel = NULL;
    g_autoptr(LrgReelClip)        title = NULL;
    g_autoptr(LrgReelRenderer)    renderer = NULL;
    g_autoptr(LrgReelGifExporter) gif = NULL;
    g_autoptr(GError)             error = NULL;

    reel = lrg_reel_new ("hello", 640, 360, 30.0, 60);

    title = lrg_reel_clip_new_with_func (draw_title, NULL, NULL);
    lrg_reel_add_clip (reel, title);

    renderer = lrg_reel_renderer_new (reel);

    gif = lrg_reel_gif_exporter_new ("reel-hello.gif", &error);
    if (gif == NULL)
    {
        g_printerr ("Failed to open output: %s\n", error->message);
        return 1;
    }

    if (!lrg_reel_renderer_render_to_exporter (renderer,
                                               LRG_REEL_EXPORTER (gif),
                                               &error))
    {
        g_printerr ("Render failed: %s\n", error->message);
        return 1;
    }

    g_print ("Wrote reel-hello.gif (%d frames)\n",
             lrg_reel_get_duration_in_frames (reel));
    return 0;
}
