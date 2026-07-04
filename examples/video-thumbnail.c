/* video-thumbnail.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Headless single-frame extraction with the LrgVideoPlayer FFmpeg backend:
 * opens a video, seeks to a timestamp, and writes that frame as a PNG.  No
 * window / GL context required.
 *
 * Requires libregnum built with FFmpeg support (FFMPEG=1).  Build, then:
 *
 *   LD_LIBRARY_PATH=build/release/lib \
 *     ./build/release/examples/video-thumbnail in.mkv 3.0 out.png
 */

#include <libregnum.h>
#include <graylib.h>
#include <stdio.h>
#include <stdlib.h>

static void
usage (const char *prog)
{
    fprintf (stderr,
             "Usage: %s VIDEO [SECONDS] [OUT.png]\n"
             "  Extract the frame at SECONDS (default 1.0) from VIDEO and\n"
             "  write it to OUT.png (default thumbnail.png).\n",
             prog);
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(GrlImage) image = NULL;
    LrgVideoTexture *vtex = NULL;
    const char *out;
    gdouble seconds;
    const guint8 *data;
    gsize size = 0;
    guint w, h;

    if (argc < 2 || g_strcmp0 (argv[1], "--help") == 0
        || g_strcmp0 (argv[1], "-h") == 0)
    {
        usage (argv[0]);
        return (argc < 2) ? 2 : 0;
    }

    seconds = (argc >= 3) ? g_ascii_strtod (argv[2], NULL) : 1.0;
    out = (argc >= 4) ? argv[3] : "thumbnail.png";

    player = lrg_video_player_new ();
    if (!lrg_video_player_open (player, argv[1], &error))
    {
        fprintf (stderr, "error: could not open '%s': %s\n",
                 argv[1], error ? error->message : "unknown");
        return 1;
    }

    if (seconds > 0.0)
        lrg_video_player_seek (player, seconds);

    vtex = lrg_video_player_get_texture (player);
    if (vtex == NULL || !lrg_video_texture_is_valid (vtex))
    {
        fprintf (stderr, "error: no frame available at %.2fs\n", seconds);
        return 1;
    }

    w = lrg_video_player_get_width (player);
    h = lrg_video_player_get_height (player);
    data = lrg_video_texture_get_data (vtex, &size);
    if (data == NULL || size != (gsize) w * h * 4)
    {
        fprintf (stderr, "error: unexpected frame buffer size\n");
        return 1;
    }

    image = grl_image_new_from_pixels ((gint) w, (gint) h,
                                       GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
                                       data);
    if (image == NULL || !grl_image_export (image, out))
    {
        fprintf (stderr, "error: could not write '%s'\n", out);
        return 1;
    }

    g_print ("Wrote %s (%ux%u, frame at %.2fs)\n", out, w, h, seconds);
    return 0;
}
