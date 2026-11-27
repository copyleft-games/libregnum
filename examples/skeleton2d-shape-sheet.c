/* skeleton2d-shape-sheet.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A contact sheet of the standard shape sheet (lrg-shape-sheet.h): the
 * demo character (examples/skeleton2d/lrg-demo-character.c) drawn once
 * per shape at weight 1, labelled with the shape's name, one row per
 * category (mouth, eyes, brows, mood, body).  Face shapes are framed on
 * the head, body shapes on the whole character.  Everything is drawn
 * on the CPU (lrg_skeleton2d_draw_to_canvas()), so no window is needed.
 *
 * Usage examples:
 *   skeleton2d-shape-sheet                       writes ./shape-sheet.png
 *   skeleton2d-shape-sheet --out /tmp/sheet      writes /tmp/sheet/shape-sheet.png
 *   skeleton2d-shape-sheet --cell 120            smaller cells
 */

#include <libregnum.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

#include "skeleton2d/lrg-demo-character.h"

#define LABEL_H     (26)
#define HEADER_W    (96)
#define MARGIN      (12)
#define TITLE_H     (40)

static const gchar *license_text =
	"skeleton2d-shape-sheet - a libregnum skeleton2d example\n"
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

/* the rows of the sheet, in the order of LrgShapeCategory */
static const struct
{
	LrgShapeCategory category;
	const gchar     *title;
	gboolean         whole_body;
} rows[] = {
	{ LRG_SHAPE_CATEGORY_MOUTH, "MOUTH", FALSE },
	{ LRG_SHAPE_CATEGORY_EYES,  "EYES",  FALSE },
	{ LRG_SHAPE_CATEGORY_BROWS, "BROWS", FALSE },
	{ LRG_SHAPE_CATEGORY_MOOD,  "MOOD",  FALSE },
	{ LRG_SHAPE_CATEGORY_BODY,  "BODY",  TRUE },
};

/*
 * Draws @skel with @shape at weight 1 into the cell at (@x, @y), @cell
 * pixels square, and labels it.  The canvas transform frames the head
 * (or the whole character) and the clip rectangle keeps the drawing in
 * its cell.
 */
static void
draw_cell (LrgImageCanvas *canvas,
           LrgSkeleton2D  *skel,
           const gchar    *shape,
           gboolean        whole_body,
           gint            x,
           gint            y,
           gint            cell)
{
	GrlImage    *img = lrg_image_canvas_get_image (canvas);
	GrlColor     card = { 255, 250, 240, 255 };
	GrlColor     edge = { 214, 204, 190, 255 };
	GrlColor     ink = { 52, 44, 60, 255 };
	GrlRectangle r;
	gfloat       fx;
	gfloat       fy;
	gfloat       fw;
	gfloat       fh;
	gfloat       zoom;
	gint         tw;
	g_autoptr(GrlVector2) size = NULL;

	/* the card */
	r.x = (gfloat)x;
	r.y = (gfloat)y;
	r.width = (gfloat)cell;
	r.height = (gfloat)(cell + LABEL_H);
	grl_image_draw_rectangle (img, &r, &edge);
	r.x += 1.0f;
	r.y += 1.0f;
	r.width -= 2.0f;
	r.height -= 2.0f;
	grl_image_draw_rectangle (img, &r, &card);

	/* pose: only this shape */
	lrg_skeleton2d_reset_pose (skel);
	lrg_demo_character_set_shape (skel, shape, 1.0f);
	lrg_skeleton2d_set_root_transform (skel, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
	lrg_skeleton2d_update_world (skel);

	/* frame: the face box, or the whole character from the hair to the
	 * hem, centred in the cell with a little air */
	if (whole_body)
	{
		fx = -LRG_DEMO_CHARACTER_HALF_W;
		fy = LRG_DEMO_CHARACTER_TOP;
		fw = 2.0f * LRG_DEMO_CHARACTER_HALF_W;
		fh = -LRG_DEMO_CHARACTER_TOP;
	}
	else
		lrg_demo_character_get_region (LRG_DEMO_REGION_FACE, &fx, &fy, &fw, &fh);
	zoom = ((gfloat)cell - 12.0f) / MAX (fw, fh);
	r.x = (gfloat)x + 1.0f;
	r.y = (gfloat)y + 1.0f;
	r.width = (gfloat)cell - 2.0f;
	r.height = (gfloat)cell - 2.0f;
	lrg_image_canvas_save (canvas);
	lrg_image_canvas_set_clip_rect (canvas, &r);
	/* the canvas applies each new transform after the earlier ones, so
	 * the steps go in the order they act on a point: centre the frame on
	 * the origin, zoom, move to the cell's centre */
	lrg_image_canvas_translate (canvas, -(fx + fw / 2.0f), -(fy + fh / 2.0f));
	lrg_image_canvas_scale (canvas, zoom, zoom);
	lrg_image_canvas_translate (canvas, (gfloat)x + (gfloat)cell / 2.0f, (gfloat)y + (gfloat)cell / 2.0f + (whole_body ? 4.0f : 0.0f));
	lrg_skeleton2d_draw_to_canvas (skel, canvas, NULL);
	lrg_image_canvas_restore (canvas);
	lrg_image_canvas_set_clip_rect (canvas, NULL);

	/* the label, centred under the picture in the bitmap font (scaled
	 * 8x8 glyphs, so no font file and no window) */
	size = grl_image_measure_text_bitmap (shape, 16);
	tw = (gint)size->x;
	grl_image_draw_text_bitmap (img, shape, x + (cell - tw) / 2, y + cell + (LABEL_H - 16) / 2, 16, &ink);
}

int
main (int    argc,
      char **argv)
{
	g_autoptr(GOptionContext) context = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(LrgSkeleton2D)  skel = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autofree gchar         *out_dir = NULL;
	g_autofree gchar         *path = NULL;
	gboolean                  license = FALSE;
	gint                      cell = 168;
	guint                     max_cols = 0;
	guint                     i;
	gint                      width;
	gint                      height;
	GrlColor                  paper = { 238, 232, 222, 255 };
	GrlColor                  ink = { 52, 44, 60, 255 };
	GrlColor                  accent = { 196, 92, 72, 255 };
	GOptionEntry              entries[] = {
		{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &out_dir, "Directory to write shape-sheet.png into (default .)", "DIR" },
		{ "cell", 'c', 0, G_OPTION_ARG_INT, &cell, "Cell size in pixels (64 .. 512, default 168)", "PX" },
		{ "license", 0, 0, G_OPTION_ARG_NONE, &license, "Show the license and exit", NULL },
		{ NULL }
	};

	/* --- command line --- */
	context = g_option_context_new ("- a contact sheet of the 2D shape sheet");
	g_option_context_add_main_entries (context, entries, NULL);
	g_option_context_set_description (context,
		"Draws the demo character once per shape of the standard shape sheet\n"
		"(visemes, eyes, brows, moods, body) at weight 1, headless.\n"
		"\n"
		"Examples:\n"
		"  skeleton2d-shape-sheet                    writes ./shape-sheet.png\n"
		"  skeleton2d-shape-sheet --out /tmp/sheet   writes /tmp/sheet/shape-sheet.png\n"
		"  skeleton2d-shape-sheet --cell 120         smaller cells\n");
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
	if (cell < 64 || cell > 512)
	{
		g_printerr ("--cell must be between 64 and 512\n");
		return 1;
	}
	if (out_dir == NULL)
		out_dir = g_strdup (".");
	if (g_mkdir_with_parents (out_dir, 0755) != 0)
	{
		g_printerr ("cannot create %s: %s\n", out_dir, g_strerror (errno));
		return 1;
	}

	/* --- layout: one row per category, as wide as the longest --- */
	for (i = 0; i < G_N_ELEMENTS (rows); i++)
		max_cols = MAX (max_cols, g_strv_length ((gchar **)lrg_shape_sheet_list (rows[i].category)));
	width = HEADER_W + (gint)max_cols * (cell + MARGIN) + MARGIN;
	height = TITLE_H + (gint)G_N_ELEMENTS (rows) * (cell + LABEL_H + MARGIN) + MARGIN;
	canvas = lrg_image_canvas_new (width, height, &paper);
	grl_image_draw_text_bitmap (lrg_image_canvas_get_image (canvas),
	                            "LIBREGNUM SHAPE SHEET  -  every shape at weight 1",
	                            MARGIN, (TITLE_H - 16) / 2, 16, &ink);

	/* --- the cells --- */
	skel = lrg_demo_character_new ();
	for (i = 0; i < G_N_ELEMENTS (rows); i++)
	{
		const gchar * const *names = lrg_shape_sheet_list (rows[i].category);
		gint                 y = TITLE_H + (gint)i * (cell + LABEL_H + MARGIN);
		guint                j;

		grl_image_draw_text_bitmap (lrg_image_canvas_get_image (canvas), rows[i].title,
		                            MARGIN, y + cell / 2 - 8, 16, &accent);
		for (j = 0; names[j] != NULL; j++)
			draw_cell (canvas, skel, names[j], rows[i].whole_body,
			           HEADER_W + (gint)j * (cell + MARGIN), y, cell);
	}

	path = g_build_filename (out_dir, "shape-sheet.png", NULL);
	if (!grl_image_export (lrg_image_canvas_get_image (canvas), path))
	{
		g_printerr ("cannot write %s\n", path);
		return 1;
	}
	g_print ("wrote %s (%d x %d, %u shapes)\n", path, width, height,
	         g_strv_length ((gchar **)lrg_shape_sheet_list_all ()));
	return 0;
}
