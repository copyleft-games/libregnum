/* skeleton2d-talking-head.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A talking character rendered headless, end to end:
 *
 * - the demo character (examples/skeleton2d/lrg-demo-character.c): an
 *   LrgSkeleton2D built in code with the full standard shape sheet;
 * - LrgLipSync reads a phoneme alignment (examples/data/hello.yaml,
 *   "Hello, I am a libregnum character") and writes viseme keys into an
 *   LrgSkeleton2DClip; --wave FILE uses the loudness of a recording
 *   instead (lrg_lip_sync_new_from_wave()) and muxes it into the video;
 * - mood keys in the same clip turn a worried face into a happy one
 *   halfway through the line;
 * - LrgSkeleton2DProcedural blinks (seeded, so every run is identical)
 *   and breathes on top of the clip, and the pose callback sways the
 *   head;
 * - LrgReelSkeleton2DClip draws it on the CPU, LrgReelRenderer renders
 *   the frames on several threads into a PNG sequence
 *   (LrgReelSeqExporter) and an MP4 (LrgReelVideoExporter, skipped when
 *   ffmpeg is not installed).
 *
 * Usage examples:
 *   skeleton2d-talking-head                           ./talking-head/frames + talking-head.mp4
 *   skeleton2d-talking-head --out /tmp/th             write into /tmp/th
 *   skeleton2d-talking-head --wave line.wav           lip sync from audio loudness
 *   skeleton2d-talking-head --scale 0.25 --no-video   a quick 128x128 smoke run
 */

#include <libregnum.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <math.h>

#include "skeleton2d/lrg-demo-character.h"

#define FRAME_SIZE   (512)
#define TAIL_SECONDS (0.4)   /* hold after the last phoneme */
#define MOOD_FADE    (0.25f) /* seconds either side of the mood switch */

static const gchar *license_text =
	"skeleton2d-talking-head - a libregnum skeleton2d example\n"
	"Copyright (C) 2026 Zach Podbielniak\n\n"
	"This program is free software: you can redistribute it and/or modify\n"
	"it under the terms of the GNU Affero General Public License as published by\n"
	"the Free Software Foundation, either version 3 of the License, or\n"
	"(at your option) any later version.\n\n"
	"This program is distributed in the hope that it will be useful,\n"
	"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
	"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
	"GNU Affero General Public License for more details.\n\n"
	"You should have received a copy of the GNU Affero General Public License\n"
	"along with this program.  If not, see <https://www.gnu.org/licenses/>.\n"
	"\nSPDX-License-Identifier: AGPL-3.0-or-later\n";

/*
 * The procedural layer, run by the reel clip after the "talk" clip each
 * frame: a gentle head sway and nod, then the blink and breathe layers.
 * It is a pure function of @seconds (frames render out of order and on
 * several threads), and the reel clip calls it under its render lock.
 */
static void
pose_layers (LrgReelSkeleton2DClip *clip,
             LrgSkeleton2D         *skel,
             gdouble                seconds,
             gpointer               user_data)
{
	LrgSkeleton2DProcedural *proc = user_data;
	gint                     head = lrg_skeleton2d_find_bone (skel, LRG_DEMO_BONE_HEAD);
	gfloat                   x;
	gfloat                   y;
	gfloat                   r;
	gfloat                   sx;
	gfloat                   sy;

	/* two slow sines that never quite repeat read as idle motion */
	if (lrg_skeleton2d_get_bone_local (skel, head, &x, &y, &r, &sx, &sy))
	{
		r += (gfloat)(3.0 * sin (seconds * 1.7) + 1.2 * sin (seconds * 3.1 + 1.0));
		y += (gfloat)(1.5 * sin (seconds * 2.3));
		lrg_skeleton2d_set_bone_local (skel, head, x, y, r, sx, sy);
	}
	lrg_skeleton2d_procedural_apply (proc, skel, seconds);
}

/* Renders every frame of @renderer into @exporter on @threads threads. */
static gboolean
render (LrgReelRenderer *renderer,
        LrgReelExporter *exporter,
        gint             threads,
        GError         **error)
{
	if (threads > 1)
		return lrg_reel_renderer_render_parallel (renderer, threads, exporter, error);
	return lrg_reel_renderer_render_to_exporter (renderer, exporter, error);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext)          context = NULL;
	g_autoptr(GError)                  error = NULL;
	g_autofree gchar                  *out_dir = NULL;
	g_autofree gchar                  *line_path = NULL;
	g_autofree gchar                  *wave_path = NULL;
	g_autofree gchar                  *frames_dir = NULL;
	g_autofree gchar                  *video_path = NULL;
	g_autoptr(LrgWaveData)             wave = NULL;
	g_autoptr(LrgLipSync)              lip = NULL;
	g_autoptr(LrgSkeleton2D)           skel = NULL;
	g_autoptr(LrgSkeleton2DClip)       talk = NULL;
	g_autoptr(LrgSkeleton2DProcedural) proc = NULL;
	g_autoptr(LrgReel)                 reel = NULL;
	g_autoptr(LrgReelGradientClip)     bg = NULL;
	g_autoptr(LrgReelSkeleton2DClip)   actor = NULL;
	g_autoptr(LrgReelRenderer)         renderer = NULL;
	g_autoptr(LrgReelSeqExporter)      seq = NULL;
	gboolean                           license = FALSE;
	gboolean                           no_video = FALSE;
	gdouble                            scale = 1.0;
	gint                               fps = 30;
	gint                               threads = 8;
	gint                               size;
	gint                               frames;
	gdouble                            duration;
	gfloat                             mid;
	GrlColor                           top = { 255, 246, 226, 255 };
	GrlColor                           bottom = { 214, 196, 224, 255 };
	GOptionEntry                       entries[] = {
		{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &out_dir, "Output directory (default ./talking-head)", "DIR" },
		{ "line", 'l', 0, G_OPTION_ARG_FILENAME, &line_path,
		  "Phoneme alignment, YAML or CSV (default examples/data/hello.yaml)", "FILE" },
		{ "wave", 'w', 0, G_OPTION_ARG_FILENAME, &wave_path,
		  "Lip-sync from this recording's loudness instead, and put it in the video", "FILE" },
		{ "scale", 's', 0, G_OPTION_ARG_DOUBLE, &scale, "Frame size as a fraction of 512 px (0.1 .. 4, default 1)", "S" },
		{ "fps", 'f', 0, G_OPTION_ARG_INT, &fps, "Frames per second (default 30)", "N" },
		{ "threads", 't', 0, G_OPTION_ARG_INT, &threads, "Render threads (default 8)", "N" },
		{ "no-video", 0, 0, G_OPTION_ARG_NONE, &no_video, "Write the PNG frames only", NULL },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &license, "Show the license and exit", NULL },
		{ NULL }
	};

	/* --- command line --- */
	context = g_option_context_new ("- a talking, blinking 2D character rendered headless");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_description (context,
		"Lip-syncs the demo character to a phoneme alignment (or a recording),\n"
		"blinks, breathes and changes mood halfway, and renders it on the CPU\n"
		"to DIR/frames/frame_NNNNN.png and DIR/talking-head.mp4 (when ffmpeg\n"
		"is installed).  Run from the repository root for the default line.\n"
		"\n"
		"Examples:\n"
		"  skeleton2d-talking-head                          ./talking-head/frames + .mp4\n"
		"  skeleton2d-talking-head --out /tmp/th            write into /tmp/th\n"
		"  skeleton2d-talking-head --line my-line.csv       your own alignment\n"
		"  skeleton2d-talking-head --wave line.wav          lip sync from audio loudness\n"
		"  skeleton2d-talking-head --scale 0.25 --no-video  a quick 128x128 smoke run\n");
	if (!g_option_context_parse (context, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 1;
	}
	if (license)
	{
		g_print ("%s", license_text);
		return 0;
	}
	if (!(scale >= 0.1 && scale <= 4.0) || fps < 1 || fps > 120 || threads < 1)
	{
		g_printerr ("--scale must be 0.1 .. 4, --fps 1 .. 120 and --threads at least 1\n");
		return 1;
	}
	if (out_dir == NULL)
		out_dir = g_strdup ("talking-head");
	if (line_path == NULL)
		line_path = g_strdup ("examples/data/hello.yaml");
	if (g_mkdir_with_parents (out_dir, 0755) != 0)
	{
		g_printerr ("cannot create %s: %s\n", out_dir, g_strerror (errno));
		return 1;
	}

	/* --- the line: a phoneme alignment, or the loudness of a recording --- */
	if (wave_path != NULL)
	{
		wave = lrg_wave_data_new_from_file (wave_path, &error);
		if (wave == NULL)
		{
			g_printerr ("%s\n", error->message);
			return 1;
		}
		lip = lrg_lip_sync_new_from_wave (wave, (gdouble)fps, 0.03f);
		if (lip == NULL)
		{
			g_printerr ("%s: no samples to lip-sync\n", wave_path);
			return 1;
		}
	}
	else
	{
		lip = lrg_lip_sync_new ();
		if (!lrg_lip_sync_load_file (lip, line_path, &error))
		{
			g_printerr ("%s\n", error->message);
			return 1;
		}
	}

	/* --- the animation: visemes, then the mood change --- */
	skel = lrg_demo_character_new ();
	talk = lrg_skeleton2d_clip_new ("talk");
	lrg_lip_sync_write_clip (lip, talk, LRG_DEMO_SLOT_MOUTH, 0.06f);
	duration = lrg_lip_sync_get_duration (lip) + TAIL_SECONDS;
	mid = (gfloat)(duration / 2.0);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SAD, 0.0f, 0.8f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SAD, mid - MOOD_FADE, 0.8f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_SAD, mid + MOOD_FADE, 0.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, mid - MOOD_FADE, 0.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, mid + MOOD_FADE, 1.0f);
	lrg_demo_character_key_shape (skel, talk, LRG_SHAPE_HAPPY, (gfloat)duration, 1.0f);
	lrg_skeleton2d_clip_set_duration (talk, (gfloat)duration);
	lrg_skeleton2d_add_clip (skel, talk);

	/* --- procedural layers: a blink every 1.4 .. 2.6 s, breathing --- */
	proc = lrg_skeleton2d_procedural_new ();
	lrg_skeleton2d_procedural_add_blink (proc, LRG_DEMO_SLOT_EYES, 1.4f, 2.6f, 0.2f, 8);
	lrg_skeleton2d_procedural_add_breathe (proc, LRG_DEMO_SLOT_BODY, 3.2f, 1.0f);

	/* --- the reel: a soft backdrop and the character standing on the
	 * bottom edge, played once (no loop) --- */
	size = ((gint)lround (FRAME_SIZE * scale) + 1) & ~1;   /* even, for H.264 */
	frames = (gint)ceil (duration * (gdouble)fps);
	reel = lrg_reel_new ("talking-head", size, size, (gdouble)fps, frames);
	bg = lrg_reel_gradient_clip_new_linear (&top, &bottom, GRL_GRADIENT_AXIS_VERTICAL);
	lrg_reel_add_clip (reel, LRG_REEL_CLIP (bg));
	actor = lrg_reel_skeleton2d_clip_new (skel, talk);
	lrg_reel_skeleton2d_clip_set_loop (actor, FALSE);
	lrg_reel_skeleton2d_clip_set_placement (actor, (gdouble)size / 2.0, (gdouble)size, scale);
	lrg_reel_skeleton2d_clip_set_pose_func (actor, pose_layers, g_object_ref (proc), g_object_unref);
	lrg_reel_add_clip (reel, LRG_REEL_CLIP (actor));
	renderer = lrg_reel_renderer_new (reel);

	/* --- PNG frames --- */
	frames_dir = g_build_filename (out_dir, "frames", NULL);
	seq = lrg_reel_seq_exporter_new (frames_dir, "frame_%05d", LRG_REEL_IMAGE_FORMAT_PNG);
	if (!render (renderer, LRG_REEL_EXPORTER (seq), threads, &error))
	{
		g_printerr ("render failed: %s\n", error->message);
		return 1;
	}
	g_print ("wrote %u frames (%d x %d, %d fps, %.2f s) to %s\n",
	         lrg_reel_seq_exporter_get_frame_count (seq), size, size, fps, duration, frames_dir);

	/* --- MP4, when ffmpeg is there --- */
	if (no_video)
		return 0;
	if (!lrg_reel_video_exporter_is_ffmpeg_available ())
	{
		g_print ("ffmpeg not found: skipping the MP4 (the PNG frames are complete)\n");
		return 0;
	}
	video_path = g_build_filename (out_dir, "talking-head.mp4", NULL);
	{
		g_autoptr(LrgReelVideoExporter) video = lrg_reel_video_exporter_new (video_path, LRG_REEL_VIDEO_CODEC_H264);

		if (wave != NULL)
			lrg_reel_video_exporter_set_audio (video, wave);
		if (!render (renderer, LRG_REEL_EXPORTER (video), threads, &error))
		{
			g_printerr ("video export failed: %s\n", error->message);
			return 1;
		}
	}
	g_print ("wrote %s\n", video_path);
	return 0;
}
