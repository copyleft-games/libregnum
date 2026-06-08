/* lrg-reel-cli.c - Command-line tool for rendering YAML-defined reels
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Usage:
 *   reel render [options] <file.yaml>   -- render to video or GIF
 *   reel still  [options] <file.yaml>   -- render a single frame to an image
 *   reel info   <file.yaml>             -- print reel metadata
 */

#include "config.h"
#include <libregnum.h>
#include <glib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Subcommand: info
 * -------------------------------------------------------------------------- */

static int
cmd_info (const gchar *yaml_path)
{
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    gdouble fps;
    gint width;
    gint height;
    gint frames;

    reel = lrg_reel_load_yaml (yaml_path, NULL, &error);
    if (reel == NULL)
    {
        g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
        return 1;
    }

    width  = lrg_reel_get_width (reel);
    height = lrg_reel_get_height (reel);
    fps    = lrg_reel_get_fps (reel);
    frames = lrg_reel_get_duration_in_frames (reel);

    g_print ("id:     %s\n", lrg_reel_get_id (reel));
    g_print ("size:   %d x %d\n", width, height);
    g_print ("fps:    %.4g\n", fps);
    g_print ("frames: %d\n", frames);

    return 0;
}

/* --------------------------------------------------------------------------
 * Subcommand: still
 * -------------------------------------------------------------------------- */

static int
cmd_still (int argc, char **argv)
{
    g_autoptr(GOptionContext) ctx = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    gchar *output = NULL;
    gint frame = 0;
    gboolean ok = FALSE;
    const gchar *yaml_path = NULL;
    GOptionEntry entries[] = {
        { "output", 'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Output image path (default: still.png)", "PATH" },
        { "frame",  'f', 0, G_OPTION_ARG_INT,      &frame,
          "Frame number to render (default: 0)", "N" },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };

    ctx = g_option_context_new ("<file.yaml> -- render a single frame");
    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_set_help_enabled (ctx, TRUE);

    if (!g_option_context_parse (ctx, &argc, &argv, &error))
    {
        g_printerr ("reel: %s\n", error->message);
        g_free (output);
        return 1;
    }

    if (argc < 2)
    {
        g_printerr ("reel: still: missing <file.yaml>\n");
        g_printerr ("%s", g_option_context_get_help (ctx, TRUE, NULL));
        g_free (output);
        return 1;
    }

    yaml_path = argv[1];

    if (output == NULL)
        output = g_strdup ("still.png");

    reel = lrg_reel_load_yaml (yaml_path, NULL, &error);
    if (reel == NULL)
    {
        g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
        g_free (output);
        return 1;
    }

    renderer = lrg_reel_renderer_new (reel);

    ok = lrg_reel_renderer_render_still (renderer, frame, output, &error);
    if (!ok)
    {
        g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
        g_free (output);
        return 1;
    }

    g_print ("wrote %s (frame %d)\n", output, frame);
    g_free (output);
    return 0;
}

/* --------------------------------------------------------------------------
 * Codec string -> enum helper
 * -------------------------------------------------------------------------- */

static gboolean
parse_codec (const gchar *name, LrgReelVideoCodec *out_codec)
{
    if (strcmp (name, "h264") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_H264;
        return TRUE;
    }
    if (strcmp (name, "vp9") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_VP9;
        return TRUE;
    }
    if (strcmp (name, "h265") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_H265;
        return TRUE;
    }
    if (strcmp (name, "prores") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_PRORES;
        return TRUE;
    }
    if (strcmp (name, "prores-alpha") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_PRORES_ALPHA;
        return TRUE;
    }
    if (strcmp (name, "vp9-alpha") == 0)
    {
        *out_codec = LRG_REEL_VIDEO_CODEC_VP9_ALPHA;
        return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------
 * Subcommand: render
 * -------------------------------------------------------------------------- */

static int
cmd_render (int argc, char **argv)
{
    g_autoptr(GOptionContext) ctx = NULL;
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgReel) reel = NULL;
    g_autoptr(LrgReelRenderer) renderer = NULL;
    gchar *output = NULL;
    gchar *codec_str = NULL;
    gint crf = -1;
    gint threads = 1;
    gboolean ok = FALSE;
    gboolean use_gif = FALSE;
    const gchar *yaml_path = NULL;
    LrgReelVideoCodec codec = LRG_REEL_VIDEO_CODEC_H264;
    gint frames = 0;
    GOptionEntry entries[] = {
        { "output",  'o', 0, G_OPTION_ARG_FILENAME, &output,
          "Output file path (default: out.mp4)", "PATH" },
        { "codec",   'c', 0, G_OPTION_ARG_STRING,   &codec_str,
          "Video codec: h264 (default), vp9, h265, prores, prores-alpha, vp9-alpha",
          "CODEC" },
        { "crf",     0,   0, G_OPTION_ARG_INT,       &crf,
          "Encoder quality CRF (lower = better; 0-51 for H.264)", "N" },
        { "threads", 't', 0, G_OPTION_ARG_INT,       &threads,
          "Threads for parallel render (0 = auto, 1 = sequential)", "N" },
        { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
    };

    ctx = g_option_context_new ("<file.yaml> -- render to video or GIF");
    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_set_help_enabled (ctx, TRUE);

    if (!g_option_context_parse (ctx, &argc, &argv, &error))
    {
        g_printerr ("reel: %s\n", error->message);
        g_free (output);
        g_free (codec_str);
        return 1;
    }

    if (argc < 2)
    {
        g_printerr ("reel: render: missing <file.yaml>\n");
        g_printerr ("%s", g_option_context_get_help (ctx, TRUE, NULL));
        g_free (output);
        g_free (codec_str);
        return 1;
    }

    yaml_path = argv[1];

    if (output == NULL)
        output = g_strdup ("out.mp4");

    /* Determine whether the output is a GIF by extension. */
    use_gif = g_str_has_suffix (output, ".gif");

    /* Parse codec (only relevant for video output). */
    codec = LRG_REEL_VIDEO_CODEC_H264;
    if (!use_gif && codec_str != NULL)
    {
        if (!parse_codec (codec_str, &codec))
        {
            g_printerr ("reel: unknown codec '%s'; "
                        "valid: h264, vp9, h265, prores, prores-alpha, vp9-alpha\n",
                        codec_str);
            g_free (output);
            g_free (codec_str);
            return 1;
        }
    }

    reel = lrg_reel_load_yaml (yaml_path, NULL, &error);
    if (reel == NULL)
    {
        g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
        g_free (output);
        g_free (codec_str);
        return 1;
    }

    frames = lrg_reel_get_duration_in_frames (reel);
    renderer = lrg_reel_renderer_new (reel);

    if (use_gif)
    {
        /* GIF path */
        g_autoptr(LrgReelGifExporter) gif_exp = NULL;

        gif_exp = lrg_reel_gif_exporter_new (output, &error);
        if (gif_exp == NULL)
        {
            g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
            g_free (output);
            g_free (codec_str);
            return 1;
        }

        if (threads != 1)
            ok = lrg_reel_renderer_render_parallel (renderer, threads,
                                                    LRG_REEL_EXPORTER (gif_exp),
                                                    &error);
        else
            ok = lrg_reel_renderer_render_to_exporter (renderer,
                                                       LRG_REEL_EXPORTER (gif_exp),
                                                       &error);
    }
    else
    {
        /* Video path */
        g_autoptr(LrgReelVideoExporter) vid_exp = NULL;

        vid_exp = lrg_reel_video_exporter_new (output, codec);

        if (crf >= 0)
            lrg_reel_video_exporter_set_crf (vid_exp, crf);

        if (threads != 1)
            ok = lrg_reel_renderer_render_parallel (renderer, threads,
                                                    LRG_REEL_EXPORTER (vid_exp),
                                                    &error);
        else
            ok = lrg_reel_renderer_render_to_exporter (renderer,
                                                       LRG_REEL_EXPORTER (vid_exp),
                                                       &error);
    }

    if (!ok)
    {
        g_printerr ("reel: %s\n", error != NULL ? error->message : "unknown error");
        g_free (output);
        g_free (codec_str);
        return 1;
    }

    g_print ("wrote %s (%d frames)\n", output, frames);
    g_free (output);
    g_free (codec_str);
    return 0;
}

/* --------------------------------------------------------------------------
 * Entry point
 * -------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    const gchar *subcmd;

    if (argc < 2)
    {
        g_printerr ("usage: reel <subcommand> [options] <file.yaml>\n");
        g_printerr ("\n");
        g_printerr ("subcommands:\n");
        g_printerr ("  render   render a reel to video or GIF\n");
        g_printerr ("  still    render a single frame to an image file\n");
        g_printerr ("  info     print reel metadata (no render)\n");
        g_printerr ("\n");
        g_printerr ("Run 'reel <subcommand> --help' for per-subcommand options.\n");
        return 2;
    }

    subcmd = argv[1];

    /* Shift argv so each sub-handler sees its own args at argv[1..]. */
    if (strcmp (subcmd, "render") == 0)
        return cmd_render (argc - 1, argv + 1);

    if (strcmp (subcmd, "still") == 0)
        return cmd_still (argc - 1, argv + 1);

    if (strcmp (subcmd, "info") == 0)
    {
        if (argc < 3)
        {
            g_printerr ("reel: info: missing <file.yaml>\n");
            return 1;
        }
        return cmd_info (argv[2]);
    }

    g_printerr ("reel: unknown subcommand '%s'\n", subcmd);
    g_printerr ("Run 'reel' with no arguments for usage.\n");
    return 2;
}
