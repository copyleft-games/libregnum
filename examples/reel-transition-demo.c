/* reel-transition-demo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Composes two video clips joined by a cross-dissolve transition on a Reel
 * timeline (LrgReelTransitionSeries) and exports the result to an MP4.  This
 * is the core composition path the cmacs video editor (vidstudio) builds on.
 *
 * The Reel video source/exporter shell out to the `ffmpeg` binary, so this
 * runs whether or not libregnum was built with the in-process FFmpeg backend
 * (FFMPEG=1) -- it only needs `ffmpeg` on the PATH.  Build, then:
 *
 *   LD_LIBRARY_PATH=build/release/lib \
 *     ./build/release/examples/reel-transition-demo a.mp4 b.mp4 out.mp4
 */

#include <libregnum.h>
#include <stdio.h>

#define OUT_WIDTH  1280
#define OUT_HEIGHT 720
#define OUT_FPS    30.0
#define SEG_SECONDS 3.0      /* each clip is shown for this long           */
#define XFADE_SECONDS 1.0    /* dissolve overlap between the two clips      */

static void
usage (const char *prog)
{
    fprintf (stderr,
             "Usage: %s VIDEO_A VIDEO_B [OUT.mp4]\n"
             "  Cross-dissolve VIDEO_A into VIDEO_B and export to OUT.mp4\n"
             "  (default out.mp4).  Requires `ffmpeg` on the PATH.\n",
             prog);
}

int
main (int argc, char *argv[])
{
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    g_autoptr(LrgReelVideoExporter) exporter = NULL;
    g_autoptr(LrgReelTransitionSeries) series = NULL;
    g_autoptr(LrgReelVideoClip) clip_a = NULL;
    g_autoptr(LrgReelVideoClip) clip_b = NULL;
    g_autoptr(LrgReelDissolveTransition) dissolve = NULL;
    g_autoptr(GError) error = NULL;
    const char *out;
    gint seg, overlap, total;

    if (argc < 3 || g_strcmp0 (argv[1], "--help") == 0
        || g_strcmp0 (argv[1], "-h") == 0)
    {
        usage (argv[0]);
        return (argc < 3) ? 2 : 0;
    }
    out = (argc >= 4) ? argv[3] : "out.mp4";

    clip_a = lrg_reel_video_clip_new_from_file (argv[1], &error);
    if (clip_a == NULL)
    {
        fprintf (stderr, "error: could not load '%s': %s\n",
                 argv[1], error ? error->message : "unknown");
        return 1;
    }
    clip_b = lrg_reel_video_clip_new_from_file (argv[2], &error);
    if (clip_b == NULL)
    {
        fprintf (stderr, "error: could not load '%s': %s\n",
                 argv[2], error ? error->message : "unknown");
        return 1;
    }

    seg = (gint) (SEG_SECONDS * OUT_FPS);
    overlap = (gint) (XFADE_SECONDS * OUT_FPS);
    total = seg + seg - overlap;

    /* Build the timeline: clip A, dissolve, clip B. */
    series = lrg_reel_transition_series_new ();
    lrg_reel_transition_series_add (series, LRG_REEL_CLIP (clip_a), seg);
    dissolve = lrg_reel_dissolve_transition_new ();
    lrg_reel_transition_set_easing (LRG_REEL_TRANSITION (dissolve),
                                    LRG_EASING_EASE_IN_OUT_CUBIC);
    lrg_reel_transition_series_add_transition (series,
                                               LRG_REEL_TRANSITION (dissolve),
                                               overlap);
    lrg_reel_transition_series_add (series, LRG_REEL_CLIP (clip_b), seg);

    reel = lrg_reel_new ("transition-demo", OUT_WIDTH, OUT_HEIGHT,
                         OUT_FPS, total);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (series));

    renderer = lrg_reel_renderer_new (reel);
    exporter = lrg_reel_video_exporter_new (out, LRG_REEL_VIDEO_CODEC_H264);

    if (!lrg_reel_renderer_render_to_exporter (renderer,
                                               LRG_REEL_EXPORTER (exporter),
                                               &error))
    {
        fprintf (stderr, "error: export failed: %s\n",
                 error ? error->message : "unknown");
        return 1;
    }

    g_print ("Wrote %s (%d frames, %.1fs cross-dissolve)\n",
             out, total, XFADE_SECONDS);
    return 0;
}
