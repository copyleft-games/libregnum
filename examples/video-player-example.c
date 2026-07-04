/* video-player-example.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Real-time video playback with LrgVideoPlayer (the FFmpeg/libav decode
 * backend).  Opens a video file given on the command line, decodes it to a
 * texture, and plays it back in a window.
 *
 * Requires libregnum built with FFmpeg support (FFMPEG=1).  Build, then:
 *
 *   LD_LIBRARY_PATH=build/release/lib \
 *     ./build/release/examples/video-player-example path/to/video.mp4
 *
 * Controls: Space pause/resume, Left/Right seek +/-5s, L toggle loop,
 *           Esc quit.
 */

#include <libregnum.h>
#include <graylib.h>
#include <stdio.h>

#define MAX_WINDOW_W 1280
#define MAX_WINDOW_H 720

static void
usage (const char *prog)
{
    fprintf (stderr,
             "Usage: %s VIDEO\n"
             "  Play VIDEO (mp4/mkv/webm/mov/...) in a window.\n"
             "  Controls: Space pause/resume, Left/Right seek, L loop, Esc quit.\n",
             prog);
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgVideoPlayer) player = NULL;
    g_autoptr(GError) error = NULL;
    GrlWindow *window = NULL;
    GrlImage *blank = NULL;
    GrlTexture *texture = NULL;
    LrgVideoTexture *vtex = NULL;
    guint vw, vh;
    gint disp_w, disp_h;
    gdouble scale;
    GrlColor bg = { 16, 16, 20, 255 };
    GrlColor white = { 255, 255, 255, 255 };
    GrlColor hud = { 220, 220, 90, 255 };

    if (argc != 2 || g_strcmp0 (argv[1], "--help") == 0
        || g_strcmp0 (argv[1], "-h") == 0)
    {
        usage (argv[0]);
        return (argc != 2) ? 2 : 0;
    }

    player = lrg_video_player_new ();
    if (!lrg_video_player_open (player, argv[1], &error))
    {
        fprintf (stderr, "error: could not open '%s': %s\n",
                 argv[1], error ? error->message : "unknown");
        return 1;
    }

    vw = lrg_video_player_get_width (player);
    vh = lrg_video_player_get_height (player);
    if (vw == 0 || vh == 0)
    {
        fprintf (stderr, "error: video has no decodable frames\n");
        return 1;
    }

    /* Fit the window to the screen budget while keeping the aspect ratio. */
    scale = 1.0;
    if ((gint) vw > MAX_WINDOW_W)
        scale = (gdouble) MAX_WINDOW_W / (gdouble) vw;
    if ((gdouble) vh * scale > MAX_WINDOW_H)
        scale = (gdouble) MAX_WINDOW_H / (gdouble) vh;
    disp_w = (gint) ((gdouble) vw * scale);
    disp_h = (gint) ((gdouble) vh * scale);

    window = grl_window_new (disp_w, disp_h, "libregnum video player");
    grl_window_set_target_fps (window, 60);

    /* A GPU texture sized to the video that we refresh every frame. */
    blank = grl_image_new_color ((gint) vw, (gint) vh, &bg);
    texture = grl_texture_new_from_image (blank);

    lrg_video_player_play (player);

    while (!grl_window_should_close (window))
    {
        gfloat delta = grl_window_get_frame_time (window);
        gdouble pos, dur;
        const guint8 *data;
        gsize size = 0;
        GrlRectangle src = { 0.0f, 0.0f, (gfloat) vw, (gfloat) vh };
        GrlRectangle dst = { 0.0f, 0.0f, (gfloat) disp_w, (gfloat) disp_h };
        GrlVector2 origin = { 0.0f, 0.0f };
        char info[256];

        /* Input. */
        if (grl_input_is_key_pressed (GRL_KEY_SPACE))
        {
            if (lrg_video_player_get_state (player) == LRG_VIDEO_STATE_PLAYING)
                lrg_video_player_pause (player);
            else
                lrg_video_player_play (player);
        }
        if (grl_input_is_key_pressed (GRL_KEY_RIGHT))
            lrg_video_player_seek (player,
                                   lrg_video_player_get_position (player) + 5.0);
        if (grl_input_is_key_pressed (GRL_KEY_LEFT))
            lrg_video_player_seek (player,
                                   lrg_video_player_get_position (player) - 5.0);
        if (grl_input_is_key_pressed (GRL_KEY_L))
            lrg_video_player_set_loop (player,
                                       !lrg_video_player_get_loop (player));

        /* Advance + upload the current frame. */
        lrg_video_player_update (player, delta);
        vtex = lrg_video_player_get_texture (player);
        if (vtex != NULL && lrg_video_texture_is_valid (vtex))
        {
            data = lrg_video_texture_get_data (vtex, &size);
            if (data != NULL && size == (gsize) vw * vh * 4)
                grl_texture_update_rec (texture, &src, data);
        }

        pos = lrg_video_player_get_position (player);
        dur = lrg_video_player_get_duration (player);
        g_snprintf (info, sizeof info, "%6.2f / %6.2f s   %s%s",
                    pos, dur,
                    lrg_video_player_get_state (player)
                        == LRG_VIDEO_STATE_PLAYING ? "playing" : "paused",
                    lrg_video_player_get_loop (player) ? "  [loop]" : "");

        grl_window_begin_drawing (window);
        grl_draw_clear_background (&bg);
        grl_draw_texture_pro (texture, &src, &dst, &origin, 0.0f, &white);
        grl_draw_text (info, 10, 10, 20, &hud);
        grl_window_end_drawing (window);
    }

    g_clear_object (&texture);
    g_clear_object (&blank);
    g_clear_object (&window);
    return 0;
}
