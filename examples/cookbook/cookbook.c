/* cookbook.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Renders the image-quality cookbook scenes (lrg-cookbook-scenes.c) to
 * PNG files, headless: no window, no GL context.  One scene with
 * --scene NAME, all twelve by default, a contact sheet with --sheet.
 *
 * Run:   ./build/release/examples/cookbook --list
 *        ./build/release/examples/cookbook --scene glow-title --size 1280x800
 *        ./build/release/examples/cookbook --out docs/examples/images --size 640x400
 *        ./build/release/examples/cookbook --sheet sheet.png --size 320x200
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>

#include "lrg-cookbook-scenes.h"

static gchar    *opt_scene = NULL;
static gchar    *opt_out = NULL;
static gchar    *opt_size = NULL;
static gchar    *opt_sheet = NULL;
static gchar    *opt_prefix = NULL;
static gboolean  opt_list = FALSE;
static gboolean  opt_pitfalls = FALSE;
static gboolean  opt_debug_boxes = FALSE;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "scene", 's', 0, G_OPTION_ARG_STRING, &opt_scene,
	  "Render only this scene (or pitfall), see --list", "NAME" },
	{ "list", 'l', 0, G_OPTION_ARG_NONE, &opt_list, "List the scenes and pitfalls and exit", NULL },
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out,
	  "Folder for the PNG files (default: current folder)", "DIR" },
	{ "size", 'S', 0, G_OPTION_ARG_STRING, &opt_size, "Image size (default: 640x400)", "WxH" },
	{ "prefix", 'p', 0, G_OPTION_ARG_STRING, &opt_prefix,
	  "File name prefix (default: cookbook-)", "TEXT" },
	{ "pitfalls", 0, 0, G_OPTION_ARG_NONE, &opt_pitfalls,
	  "Also render the pitfall comparisons when rendering all scenes", NULL },
	{ "sheet", 0, 0, G_OPTION_ARG_FILENAME, &opt_sheet,
	  "Also write a 4x3 contact sheet of the twelve scenes to FILE", "FILE" },
	{ "debug-boxes", 'd', 0, G_OPTION_ARG_NONE, &opt_debug_boxes,
	  "Draw the frame-layout boxes over scenes that have a layout", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

/* parse "WxH" */
static gboolean
parse_size (const gchar *text,
            guint       *out_w,
            guint       *out_h)
{
	guint w = 0;
	guint h = 0;
	gchar tail = '\0';

	if (sscanf (text, "%ux%u%c", &w, &h, &tail) != 2)
		return FALSE;
	if (w < 16 || h < 16 || w > 8192 || h > 8192)
		return FALSE;
	*out_w = w;
	*out_h = h;
	return TRUE;
}

/*
 * render_scene:
 * Renders one entry, draws the layout overlay when asked, writes
 * DIR/PREFIXNAME.png and returns the image (for the contact sheet).
 */
static GrlImage *
render_scene (const LrgCookbookScene *scene,
              guint                   width,
              guint                   height,
              const gchar            *dir,
              const gchar            *prefix)
{
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autofree gchar         *file = NULL;
	g_autofree gchar         *path = NULL;
	GrlImage                 *image;
	gint64                    t0;

	t0 = g_get_monotonic_time ();
	image = scene->func (width, height, &layout);
	if (image == NULL)
	{
		g_printerr ("%s: rendering failed\n", scene->name);
		return NULL;
	}

	/* the overlay draws onto a canvas wrapping the image */
	if (opt_debug_boxes && layout != NULL)
	{
		g_autoptr(LrgImageCanvas) canvas = lrg_image_canvas_new_for_image (image);

		lrg_image_canvas_set_quality (canvas);
		lrg_frame_layout_draw_debug (layout, canvas);
	}

	file = g_strdup_printf ("%s%s.png", prefix, scene->name);
	path = g_build_filename (dir, file, NULL);
	if (!grl_image_export (image, path))
	{
		g_printerr ("%s: could not write %s\n", scene->name, path);
		g_object_unref (image);
		return NULL;
	}
	g_print ("%-24s %4ux%-4u %6.2f s  %s\n", scene->name, width, height,
	         (gdouble) (g_get_monotonic_time () - t0) / G_USEC_PER_SEC, path);
	return image;
}

/* place the twelve images on a 4 x 3 grid with a thin gutter */
static gboolean
write_sheet (GPtrArray   *images,
             guint        width,
             guint        height,
             const gchar *path)
{
	g_autoptr(LrgImageCanvas) sheet = NULL;
	GrlColor                  bg = { 12, 12, 16, 255 };
	guint                     gap = 4;
	guint                     i;

	sheet = lrg_image_canvas_new ((gint) (4 * width + 5 * gap), (gint) (3 * height + 4 * gap), &bg);
	for (i = 0; i < images->len; i++)
	{
		GrlImage *image = g_ptr_array_index (images, i);

		if (image == NULL)
			continue;
		lrg_image_canvas_composite (sheet, image, GRL_PORTER_DUFF_SRC_OVER,
		                            (gint) (gap + (i % 4) * (width + gap)),
		                            (gint) (gap + (i / 4) * (height + gap)));
	}
	return grl_image_export (lrg_image_canvas_get_image (sheet), path);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GPtrArray)      images = NULL;
	const LrgCookbookScene   *scenes;
	const LrgCookbookScene   *pitfalls;
	const gchar              *dir;
	const gchar              *prefix;
	guint                     n_scenes;
	guint                     n_pitfalls;
	guint                     width = 640;
	guint                     height = 400;
	guint                     i;
	gint                      status = 0;

	ctx = g_option_context_new ("- render the libregnum image-quality cookbook scenes");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Renders headless on the CPU; no display is needed.\n"
		"\n"
		"Examples:\n"
		"  cookbook --list                                  # scene names and summaries\n"
		"  cookbook                                         # all twelve, 640x400, ./cookbook-*.png\n"
		"  cookbook --scene sdf-character-bust --size 1280x800\n"
		"  cookbook --out shots --size 320x200 --sheet shots/sheet.png\n"
		"  cookbook --scene speech-bubble-layout --debug-boxes\n"
		"  cookbook --pitfalls --out docs/examples/images\n"
		"\n"
		"Text uses DejaVu Sans, Liberation Sans, Cantarell or Noto Sans when\n"
		"installed; set LRG_COOKBOOK_FONT=/path/to/font.ttf to choose another.\n");

	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("cookbook - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n"
		         "This is free software: you are free to change and redistribute it.\n"
		         "There is NO WARRANTY, to the extent permitted by law.\n");
		return 0;
	}

	/* only warnings from raylib's file I/O, not a line per PNG */
	SetTraceLogLevel (LOG_WARNING);

	scenes = lrg_cookbook_get_scenes (&n_scenes);
	pitfalls = lrg_cookbook_get_pitfalls (&n_pitfalls);
	if (opt_list)
	{
		for (i = 0; i < n_scenes; i++)
			g_print ("%-24s %s\n", scenes[i].name, scenes[i].summary);
		for (i = 0; i < n_pitfalls; i++)
			g_print ("%-24s %s (pitfall)\n", pitfalls[i].name, pitfalls[i].summary);
		return 0;
	}
	if (opt_size != NULL && !parse_size (opt_size, &width, &height))
	{
		g_printerr ("--size: expected WxH between 16x16 and 8192x8192, got \"%s\"\n", opt_size);
		return 2;
	}

	/* the output folder must exist */
	dir = opt_out != NULL ? opt_out : ".";
	prefix = opt_prefix != NULL ? opt_prefix : "cookbook-";
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_printerr ("--out: cannot create %s\n", dir);
		return 1;
	}

	/* one scene */
	if (opt_scene != NULL)
	{
		const LrgCookbookScene *scene = lrg_cookbook_find_scene (opt_scene);
		GrlImage               *image;

		if (scene == NULL)
		{
			g_printerr ("unknown scene \"%s\" (see --list)\n", opt_scene);
			return 2;
		}
		image = render_scene (scene, width, height, dir, prefix);
		if (image == NULL)
			return 1;
		g_object_unref (image);
		return 0;
	}

	/* all scenes, then the pitfalls and the sheet when asked */
	images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i = 0; i < n_scenes; i++)
	{
		GrlImage *image = render_scene (&scenes[i], width, height, dir, prefix);
		GrlColor  black = { 0, 0, 0, 255 };

		if (image == NULL)
		{
			status = 1;
			image = grl_image_new_color ((gint) width, (gint) height, &black);
		}
		g_ptr_array_add (images, image);
	}
	if (opt_pitfalls)
	{
		for (i = 0; i < n_pitfalls; i++)
		{
			GrlImage *image = render_scene (&pitfalls[i], width, height, dir, prefix);

			if (image == NULL)
				status = 1;
			else
				g_object_unref (image);
		}
	}
	if (opt_sheet != NULL)
	{
		if (!write_sheet (images, width, height, opt_sheet))
		{
			g_printerr ("--sheet: could not write %s\n", opt_sheet);
			status = 1;
		}
		else
			g_print ("sheet: %s\n", opt_sheet);
	}
	return status;
}
