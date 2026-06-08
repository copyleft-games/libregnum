/* reel-showcase.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A fuller Reel example showing animated layers (spring + interpolate), a
 * looping sub-sequence, a synthesized audio track, and muxed MP4 output (with
 * a GIF fallback when ffmpeg is unavailable).  Run from the repo root:
 *
 *   LD_LIBRARY_PATH=build/release/lib ./build/release/examples/reel-showcase
 *
 * Writes "reel-showcase.mp4" (or "reel-showcase.gif").
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>

#define WIDTH   640
#define HEIGHT  360
#define FPS     30.0
#define FRAMES  90      /* 3 seconds */

/* ---- background: a vertical gradient ---- */
static void
draw_background (LrgReelClip    *clip,
                 LrgReelContext *ctx,
                 LrgImageCanvas *canvas,
                 gpointer        user_data)
{
    GrlColor top = { 18, 22, 48, 255 };
    GrlColor bottom = { 6, 8, 16, 255 };

    lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, WIDTH, HEIGHT,
                                         &top, &bottom,
                                         GRL_GRADIENT_AXIS_VERTICAL);
}

/* ---- title: springs up and fades in ---- */
static void
draw_title (LrgReelClip    *clip,
            LrgReelContext *ctx,
            LrgImageCanvas *canvas,
            gpointer        user_data)
{
    gint     frame = lrg_reel_context_get_frame (ctx);
    GrlColor fg = { 255, 240, 200, 255 };
    gdouble  opacity;
    gdouble  y;

    opacity = lrg_reel_interpolate_clamped (frame, 0.0, 18.0, 0.0, 1.0,
                                            LRG_EASING_EASE_OUT_CUBIC);
    y = lrg_reel_spring (frame, FPS, NULL, 120.0, 70.0);

    fg.a = (guint8) (opacity * 255.0);
    grl_image_draw_text_bitmap (lrg_image_canvas_get_image (canvas),
                                "REEL SHOWCASE", WIDTH / 2 - 150, (gint) y,
                                28, &fg);
}

/* ---- a puck that slides across each loop iteration ---- */
static void
draw_puck (LrgReelClip    *clip,
           LrgReelContext *ctx,
           LrgImageCanvas *canvas,
           gpointer        user_data)
{
    gint     frame = lrg_reel_context_get_frame (ctx); /* 0..29 within a loop */
    GrlColor fill = { 90, 200, 255, 255 };
    gdouble  x;

    /* Ease the puck from left to right over the 30-frame loop period. */
    x = lrg_reel_interpolate_clamped (frame, 0.0, 29.0, 60.0, WIDTH - 60.0,
                                      LRG_EASING_EASE_IN_OUT_CUBIC);

    lrg_image_canvas_fill_circle (canvas, (gint) x, 250, 18, &fill);
}

/* ---- synthesize a short sine tone ---- */
static LrgWaveData *
make_tone (gfloat  freq,
           gfloat  duration,
           guint   rate)
{
    LrgWaveData *wave = lrg_wave_data_new_procedural (rate, 1, duration);
    guint        fc = lrg_wave_data_get_frame_count (wave);
    gfloat      *samples = g_new (gfloat, fc);
    guint        i;

    for (i = 0; i < fc; i++)
        samples[i] = 0.25f * sinf (2.0f * (gfloat) G_PI * freq *
                                   (gfloat) i / (gfloat) rate);

    lrg_wave_data_set_samples (wave, samples, fc);
    g_free (samples);

    return wave;
}

int
main (void)
{
    g_autoptr(LrgReel)             reel = NULL;
    g_autoptr(LrgReelClip)         bg = NULL;
    g_autoptr(LrgReelClip)         title = NULL;
    g_autoptr(LrgReelClip)         puck = NULL;
    g_autoptr(LrgReelSequence)     loop = NULL;
    g_autoptr(LrgReelRenderer)     renderer = NULL;
    g_autoptr(LrgReelAudioTrack)   audio = NULL;
    g_autoptr(LrgWaveData)         tone = NULL;
    g_autoptr(LrgWaveData)         mixed = NULL;
    g_autoptr(GError)              error = NULL;
    GrlColor                       clear = { 0, 0, 0, 255 };

    reel = lrg_reel_new ("showcase", WIDTH, HEIGHT, FPS, FRAMES);

    renderer = lrg_reel_renderer_new (reel);
    lrg_reel_renderer_set_background (renderer, &clear);

    bg = lrg_reel_clip_new_with_func (draw_background, NULL, NULL);
    lrg_reel_add_clip (reel, bg);

    /* The puck repeats its slide every 30 frames for the whole reel. */
    puck = lrg_reel_clip_new_with_func (draw_puck, NULL, NULL);
    loop = lrg_reel_sequence_new_loop (30, 3);
    lrg_reel_sequence_add_child (loop, puck);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (loop));

    title = lrg_reel_clip_new_with_func (draw_title, NULL, NULL);
    lrg_reel_add_clip (reel, title);

    /* Audio: a gentle A4 tone mixed across the whole reel. */
    audio = lrg_reel_audio_track_new (FPS);
    tone = make_tone (440.0f, (gfloat) FRAMES / (gfloat) FPS, 44100);
    lrg_reel_audio_track_add (audio, tone, 0, 1.0, 0.0, 0.0);
    mixed = lrg_reel_audio_track_mix (audio, 44100, 1, FRAMES, &error);
    if (mixed == NULL)
    {
        g_printerr ("Audio mix failed: %s\n", error->message);
        return 1;
    }

    if (lrg_reel_video_exporter_is_ffmpeg_available ())
    {
        g_autoptr(LrgReelVideoExporter) vid =
            lrg_reel_video_exporter_new ("reel-showcase.mp4",
                                         LRG_REEL_VIDEO_CODEC_H264);

        lrg_reel_video_exporter_set_audio (vid, mixed);

        if (!lrg_reel_renderer_render_to_exporter (renderer,
                                                   LRG_REEL_EXPORTER (vid),
                                                   &error))
        {
            g_printerr ("Video render failed: %s\n", error->message);
            return 1;
        }
        g_print ("Wrote reel-showcase.mp4 (with audio)\n");
        return 0;
    }
    else
    {
        g_autoptr(LrgReelGifExporter) gif =
            lrg_reel_gif_exporter_new ("reel-showcase.gif", &error);

        if (gif == NULL ||
            !lrg_reel_renderer_render_to_exporter (renderer,
                                                   LRG_REEL_EXPORTER (gif),
                                                   &error))
        {
            g_printerr ("GIF render failed: %s\n", error->message);
            return 1;
        }
        g_print ("ffmpeg not found; wrote reel-showcase.gif (no audio)\n");
        return 0;
    }
}
