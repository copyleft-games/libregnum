/* reel-captions-demo.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A 1280x720 title sequence that leans on everything the 0.13.5 .. 0.13.9
 * reel fixes made work:
 *
 *   - translucent clips blend (0.13.8): a 45%-black lower-third band, glass
 *     panels and sun haze drawn by render callbacks with plain graylib
 *     primitives, and a translucent solid that tints the night scene;
 *   - text alignment anchors on text-x (0.13.6): a title centred on the
 *     frame's middle and a credit right-aligned to the safe margin, with no
 *     max-width;
 *   - a dissolve and an iris between scenes in a transition series, rendered
 *     on every core with render_parallel (0.13.5: no corrupt overlaps);
 *   - H.264 through whatever encoder this ffmpeg can run, fed raw frames
 *     (0.13.7, 0.13.9); without ffmpeg it writes the stills only.
 *
 * Usage:
 *   reel-captions-demo [--out DIR] [--scale F] [--no-video]
 *
 * Writes DIR/reel-captions-demo.mp4 and three stills,
 * DIR/reel-captions-demo-{day,dissolve,night}.png.
 */

#include <libregnum.h>
#include <graylib.h>
#include <math.h>
#include <stdlib.h>

#define BASE_W      (1280)
#define BASE_H      (720)
#define FPS         (24.0)
#define SCENE       (72)    /* frames per scene, 3 s */
#define FADE        (24)    /* transition length, 1 s */

static const gchar *license_text =
    "reel-captions-demo is part of libregnum.\n"
    "Copyright (C) 2026 Zach Podbielniak\n\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU Affero General Public License as published\n"
    "by the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU Affero General Public License for more details.\n";

/* Everything in the scene is laid out on a 1280x720 grid and scaled by
 * this factor, so --scale 0.25 renders a quick 320x180 version. */
static gdouble scale = 1.0;

static gint
S (gdouble v)
{
    return (gint) floor (v * scale + 0.5);
}

/* ---------------------------------------------------------------------------
 * Day scene: a sky gradient, a hazy sun and drifting translucent clouds.
 * The haze and clouds are translucent circles drawn with the plain graylib
 * primitives; before 0.13.8 they replaced the sky and came out opaque.
 * ------------------------------------------------------------------------- */
static void
draw_day (LrgReelClip    *clip,
          LrgReelContext *ctx,
          LrgImageCanvas *canvas,
          gpointer        user_data)
{
    GrlImage *img = lrg_image_canvas_get_image (canvas);
    GrlColor  top = { 64, 132, 214, 255 };
    GrlColor  bottom = { 196, 226, 250, 255 };
    GrlColor  sun = { 255, 236, 170, 255 };
    GrlColor  haze = { 255, 230, 160, 60 };
    GrlColor  cloud = { 255, 255, 255, 150 };
    gint      f = lrg_reel_context_get_frame (ctx);
    gint      i;

    lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, S (BASE_W), S (BASE_H),
                                         &top, &bottom,
                                         GRL_GRADIENT_AXIS_VERTICAL);

    /* Three stacked haze rings around an opaque sun. */
    for (i = 3; i >= 1; i--)
        grl_image_draw_circle (img, S (980), S (190), S (60 + i * 34), &haze);
    grl_image_draw_circle (img, S (980), S (190), S (60), &sun);

    /* Clouds: overlapping translucent puffs drift left to right. */
    for (i = 0; i < 4; i++)
    {
        gint cx = S (fmod (140.0 + i * 330.0 + f * 3.0, BASE_W + 200.0) - 100.0);
        gint cy = S (150 + (i % 2) * 90);

        grl_image_draw_circle (img, cx, cy, S (55), &cloud);
        grl_image_draw_circle (img, cx + S (60), cy + S (10), S (45), &cloud);
        grl_image_draw_circle (img, cx - S (55), cy + S (15), S (40), &cloud);
    }
}

/* ---------------------------------------------------------------------------
 * Night scene: dark gradient, stars, and two frosted glass panels.
 * ------------------------------------------------------------------------- */
static void
draw_night (LrgReelClip    *clip,
            LrgReelContext *ctx,
            LrgImageCanvas *canvas,
            gpointer        user_data)
{
    GrlImage *img = lrg_image_canvas_get_image (canvas);
    GrlColor  top = { 8, 10, 30, 255 };
    GrlColor  bottom = { 40, 30, 80, 255 };
    GrlColor  star = { 255, 255, 230, 255 };
    GrlColor  glass = { 200, 220, 255, 70 };
    GrlColor  rim = { 220, 235, 255, 160 };
    gint      f = lrg_reel_context_get_frame (ctx);
    gint      i;

    lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, S (BASE_W), S (BASE_H),
                                         &top, &bottom,
                                         GRL_GRADIENT_AXIS_VERTICAL);

    /* Deterministic star field; twinkle by frame. */
    for (i = 0; i < 90; i++)
    {
        guint32 h = (guint32) i * 2654435761u;
        gint    x = S ((gdouble) (h % BASE_W));
        gint    y = S ((gdouble) ((h >> 11) % (BASE_H / 2)));

        star.a = (guint8) (140 + ((h >> 5) + (guint32) f * 7u) % 116u);
        grl_image_draw_pixel (img, x, y, &star);
    }

    /* Frosted panels: translucent fill plus a brighter outline. */
    for (i = 0; i < 2; i++)
    {
        g_autoptr(GrlRectangle) r =
            grl_rectangle_new ((gfloat) S (160 + i * 520), (gfloat) S (170),
                               (gfloat) S (420), (gfloat) S (250));

        grl_image_draw_rectangle (img, r, &glass);
        grl_image_draw_rectangle_lines (img, r, MAX (1, S (3)), &rim);
    }
}

/* ---------------------------------------------------------------------------
 * Scene assembly
 * ------------------------------------------------------------------------- */

/* Adds a text clip; align anchors on text_x (no max-width). */
static void
add_text (LrgReel          *reel,
          const gchar      *text,
          LrgReelTextAlign  align,
          gint              x,
          gint              y,
          gdouble           size,
          gint              from,
          gint              frames)
{
    GrlColor         white = { 250, 250, 250, 255 };
    GrlColor         shadow = { 0, 0, 0, 150 };
    LrgReelTextClip *t = lrg_reel_text_clip_new (text);

    lrg_reel_text_clip_set_color (t, &white);
    lrg_reel_text_clip_set_font_size (t, size * scale);
    lrg_reel_text_clip_set_align (t, align);
    lrg_reel_text_clip_set_text_x (t, x);
    lrg_reel_text_clip_set_text_y (t, y);
    lrg_reel_text_clip_set_shadow (t, TRUE);
    lrg_reel_text_clip_set_shadow_color (t, &shadow);
    lrg_reel_text_clip_set_shadow_offset (t, MAX (1, S (3)), MAX (1, S (3)));
    lrg_reel_clip_set_from_frame (LRG_REEL_CLIP (t), from);
    lrg_reel_clip_set_duration_in_frames (LRG_REEL_CLIP (t), frames);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (t));
    g_object_unref (t);
}

static LrgReel *
build_reel (void)
{
    LrgReel                 *reel;
    LrgReelTransitionSeries *series = lrg_reel_transition_series_new ();
    LrgReelClip             *day = lrg_reel_clip_new_with_func (draw_day, NULL, NULL);
    LrgReelClip             *night = lrg_reel_clip_new_with_func (draw_night, NULL, NULL);
    LrgReelSequence         *dusk = lrg_reel_sequence_new (0, SCENE);
    LrgReelClip             *day2 = lrg_reel_clip_new_with_func (draw_day, NULL, NULL);
    LrgReelDissolveTransition *dissolve = lrg_reel_dissolve_transition_new ();
    LrgReelIrisTransition   *iris = lrg_reel_iris_transition_new ();
    GrlColor                 dusk_tint = { 120, 40, 90, 110 };
    GrlColor                 band = { 0, 0, 0, 115 };
    LrgReelSolidClip        *tint = lrg_reel_solid_clip_new (&dusk_tint);
    LrgReelShapeClip        *lower = NULL;
    gint                     total;

    /* Scene 2 ("dusk") is the day scene under a translucent solid: the
     * solid tints it instead of replacing it. */
    lrg_reel_sequence_add_child (dusk, day2);
    lrg_reel_sequence_add_child (dusk, LRG_REEL_CLIP (tint));

    lrg_reel_transition_series_add (series, day, SCENE);
    lrg_reel_transition_series_add_transition (series, LRG_REEL_TRANSITION (dissolve), FADE);
    lrg_reel_transition_series_add (series, LRG_REEL_CLIP (dusk), SCENE);
    lrg_reel_transition_series_add_transition (series, LRG_REEL_TRANSITION (iris), FADE);
    lrg_reel_transition_series_add (series, night, SCENE);
    total = lrg_reel_transition_series_get_total_frames (series);

    reel = lrg_reel_new ("captions-demo", S (BASE_W), S (BASE_H), FPS, total);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (series));

    /* The 45%-black lower third over everything. */
    lower = lrg_reel_shape_clip_new_rect (0, S (540), S (BASE_W), S (110));
    lrg_reel_shape_clip_set_fill_color (lower, &band);
    lrg_reel_add_clip (reel, LRG_REEL_CLIP (lower));

    /* Title centred on the frame's middle, credit right-aligned to the
     * 40 px safe margin, captions centred in the band. */
    add_text (reel, "LIBREGNUM REEL", LRG_REEL_TEXT_ALIGN_CENTER, S (BASE_W / 2), S (60), 56, 0, total);
    add_text (reel, "captions demo  v0.13.9", LRG_REEL_TEXT_ALIGN_RIGHT, S (BASE_W - 40), S (670), 20, 0, total);
    add_text (reel, "Translucent band, glass and clouds blend", LRG_REEL_TEXT_ALIGN_CENTER,
              S (BASE_W / 2), S (578), 32, 0, SCENE);
    add_text (reel, "A translucent solid tints the dusk", LRG_REEL_TEXT_ALIGN_CENTER,
              S (BASE_W / 2), S (578), 32, SCENE, SCENE - FADE);
    add_text (reel, "Frosted glass at night", LRG_REEL_TEXT_ALIGN_CENTER,
              S (BASE_W / 2), S (578), 32, 2 * SCENE - FADE, total - (2 * SCENE - FADE));

    g_object_unref (series);
    g_object_unref (day);
    g_object_unref (day2);
    g_object_unref (night);
    g_object_unref (dusk);
    g_object_unref (tint);
    g_object_unref (dissolve);
    g_object_unref (iris);
    g_object_unref (lower);
    return reel;
}

int
main (int    argc,
      char **argv)
{
    g_autofree gchar          *out = NULL;
    gboolean                   no_video = FALSE;
    gboolean                   license = FALSE;
    gdouble                    opt_scale = 1.0;
    g_autoptr(GOptionContext)  octx = NULL;
    g_autoptr(GError)          error = NULL;
    g_autoptr(LrgReel)         reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    const gchar               *stills[] = { "day", "dissolve", "night" };
    gint                       still_frames[3];
    gint                       i;
    GOptionEntry entries[] = {
        { "out", 'o', 0, G_OPTION_ARG_FILENAME, &out,
          "Directory to write the video and stills to (default: .)", "DIR" },
        { "scale", 's', 0, G_OPTION_ARG_DOUBLE, &opt_scale,
          "Render at this fraction of 1280x720 (default 1.0)", "F" },
        { "no-video", 0, 0, G_OPTION_ARG_NONE, &no_video,
          "Write the stills only, skip the ffmpeg export", NULL },
        { "license", 0, 0, G_OPTION_ARG_NONE, &license,
          "Print the license (AGPL-3.0-or-later) and exit", NULL },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };

    octx = g_option_context_new ("- render the reel captions demo");
    g_option_context_add_main_entries (octx, entries, NULL);
    g_option_context_set_description (octx,
        "Examples:\n"
        "  reel-captions-demo                       # 720p MP4 + stills in .\n"
        "  reel-captions-demo --out /tmp/demo       # into /tmp/demo\n"
        "  reel-captions-demo --scale 0.25 --no-video   # quick 320x180 stills\n");
    if (!g_option_context_parse (octx, &argc, &argv, &error))
    {
        g_printerr ("reel-captions-demo: %s\n", error->message);
        return 1;
    }
    if (license)
    {
        g_print ("%s", license_text);
        return 0;
    }
    if (opt_scale <= 0.05 || opt_scale > 4.0)
    {
        g_printerr ("reel-captions-demo: --scale must be in (0.05, 4]\n");
        return 1;
    }
    scale = opt_scale;
    if (out == NULL)
        out = g_strdup (".");
    if (g_mkdir_with_parents (out, 0755) != 0)
    {
        g_printerr ("reel-captions-demo: cannot create %s\n", out);
        return 1;
    }

    reel = build_reel ();
    renderer = lrg_reel_renderer_new (reel);

    /* Three stills: mid day, mid dissolve, mid night. */
    still_frames[0] = SCENE / 3;
    still_frames[1] = SCENE - FADE / 2;
    still_frames[2] = lrg_reel_get_duration_in_frames (reel) - SCENE / 3;
    for (i = 0; i < 3; i++)
    {
        g_autofree gchar *name = g_strdup_printf ("reel-captions-demo-%s.png", stills[i]);
        g_autofree gchar *path = g_build_filename (out, name, NULL);

        if (!lrg_reel_renderer_render_still (renderer, still_frames[i], path, &error))
        {
            g_printerr ("reel-captions-demo: %s\n", error->message);
            return 1;
        }
        g_print ("wrote %s\n", path);
    }

    /* The video: every core, H.264 with whatever encoder works here. */
    if (!no_video)
    {
        g_autofree gchar *path = g_build_filename (out, "reel-captions-demo.mp4", NULL);
        g_autoptr(LrgReelVideoExporter) vid = NULL;

        if (!lrg_reel_video_exporter_is_ffmpeg_available ())
        {
            g_print ("ffmpeg not found; skipping the video\n");
            return 0;
        }
        vid = lrg_reel_video_exporter_new (path, LRG_REEL_VIDEO_CODEC_H264);
        if (!lrg_reel_renderer_render_parallel (renderer, 0, LRG_REEL_EXPORTER (vid), &error))
        {
            g_printerr ("reel-captions-demo: %s\n", error->message);
            return 1;
        }
        g_print ("wrote %s (H.264 via %s, %d frames)\n", path,
                 lrg_reel_video_exporter_get_encoder (vid),
                 lrg_reel_get_duration_in_frames (reel));
    }

    return 0;
}
