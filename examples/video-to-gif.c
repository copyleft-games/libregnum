/* video-to-gif.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless: decode the first few seconds of a video with the LrgVideoPlayer
 * FFmpeg backend and write them out as an animated GIF (downscaled).  Shows
 * sequential frame pulling from the decoder.
 *
 * Requires libregnum built with FFmpeg support (FFMPEG=1).  Build, then:
 *
 *   LD_LIBRARY_PATH=build/release/lib \
 *     ./build/release/examples/video-to-gif in.mp4 out.gif 5.0
 */

#include <libregnum.h>
#include <graylib.h>
#include <stdio.h>

#define GIF_MAX_WIDTH 480

static void
usage (const char *prog)
{
    fprintf (stderr,
             "Usage: %s VIDEO [OUT.gif] [SECONDS]\n"
             "  Convert the first SECONDS (default 5) of VIDEO to an animated\n"
             "  GIF, downscaled to %d px wide.\n",
             prog, GIF_MAX_WIDTH);
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    GrlGifWriter *gif = NULL;
    const char *out;
    gdouble cap, fps, dt;
    gint delay_cs, gif_w, gif_h;
    guint w, h;
    int rc = 0;

    if (argc < 2 || g_strcmp0 (argv[1], "--help") == 0
        || g_strcmp0 (argv[1], "-h") == 0)
    {
        usage (argv[0]);
        return (argc < 2) ? 2 : 0;
    }

    out = (argc >= 3) ? argv[2] : "out.gif";
    cap = (argc >= 4) ? g_ascii_strtod (argv[3], NULL) : 5.0;

    player = lrg_video_player_new ();
    if (!lrg_video_player_open (player, argv[1], &error))
    {
        fprintf (stderr, "error: could not open '%s': %s\n",
                 argv[1], error ? error->message : "unknown");
        return 1;
    }

    w = lrg_video_player_get_width (player);
    h = lrg_video_player_get_height (player);
    fps = lrg_video_player_get_frame_rate (player);
    if (fps <= 0.0)
        fps = 25.0;
    dt = 1.0 / fps;
    delay_cs = (gint) (100.0 / fps + 0.5);
    if (delay_cs < 1)
        delay_cs = 1;

    gif_w = ((gint) w > GIF_MAX_WIDTH) ? GIF_MAX_WIDTH : (gint) w;
    gif_h = (gint) ((gdouble) h * (gdouble) gif_w / (gdouble) w);
    if (gif_h < 1)
        gif_h = 1;

    gif = grl_gif_writer_new (out, gif_w, gif_h, 0, &error);
    if (gif == NULL)
    {
        fprintf (stderr, "error: could not open '%s': %s\n",
                 out, error ? error->message : "unknown");
        return 1;
    }

    /* Pull frames sequentially: grab the current frame, then advance. */
    lrg_video_player_play (player);
    for (;;)
    {
        LrgVideoTexture *vtex = lrg_video_player_get_texture (player);
        const guint8 *data;
        gsize size = 0;

        if (lrg_video_player_get_position (player) >= cap)
            break;

        if (vtex != NULL && lrg_video_texture_is_valid (vtex))
        {
            data = lrg_video_texture_get_data (vtex, &size);
            if (data != NULL && size == (gsize) w * h * 4)
            {
                g_autoptr(GrlImage) frame =
                    grl_image_new_from_pixels (
                        (gint) w, (gint) h,
                        GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, data);

                if (frame != NULL
                    && !grl_gif_writer_add_frame (gif, frame, delay_cs, &error))
                {
                    fprintf (stderr, "error: add frame failed: %s\n",
                             error ? error->message : "unknown");
                    rc = 1;
                    break;
                }
            }
        }

        if (lrg_video_player_get_state (player) == LRG_VIDEO_STATE_FINISHED)
            break;

        lrg_video_player_update (player, (gfloat) dt);
    }

    if (!grl_gif_writer_close (gif, (rc == 0) ? &error : NULL) && rc == 0)
    {
        fprintf (stderr, "error: could not finish '%s': %s\n",
                 out, error ? error->message : "unknown");
        rc = 1;
    }
    g_object_unref (gif);

    if (rc == 0)
        g_print ("Wrote %s (%dx%d @ %.1f fps)\n", out, gif_w, gif_h, fps);
    return rc;
}
