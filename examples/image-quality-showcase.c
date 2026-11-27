/* image-quality-showcase.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * One scene drawn twice, side by side: on the left with a plain
 * lrg_image_canvas_new () (graylib's pixel-exact defaults: REPLACE
 * blending, gamma-space mixing, aliased edges), on the right with the
 * quality preset, 3x supersampling and the CPU post stack (bloom, ACES
 * tone map, vignette, grain).  The drawing code is the same function
 * for both halves; only the canvas and the finishing differ.  Headless:
 * no window, no GL context.
 *
 * Run:   ./build/release/examples/image-quality-showcase
 *        ./build/release/examples/image-quality-showcase --out shots --size 1920x960
 */

#include <stdio.h>
#include <math.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>

static gchar    *opt_out = NULL;
static gchar    *opt_size = NULL;
static gchar    *opt_font = NULL;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder for the PNG (default: current folder)", "DIR" },
	{ "size", 's', 0, G_OPTION_ARG_STRING, &opt_size, "Size of the whole image (default: 1280x640)", "WxH" },
	{ "font", 'f', 0, G_OPTION_ARG_FILENAME, &opt_font, "TrueType font for the text (default: DejaVu Sans Bold)", "FILE" },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

#define FONT_DEFAULT "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"

static void
set_color (GrlColor *c,
           guint8    r,
           guint8    g,
           guint8    b,
           guint8    a)
{
	c->r = r;
	c->g = g;
	c->b = b;
	c->a = a;
}

/*
 * draw_scene:
 * The scene, in a size x size square, for a canvas whose pixels are
 * f times finer (f = 1 for the plain canvas, 3 for the supersampled
 * one).  Geometry goes through the transform stack (scaled by f);
 * gradients and text ignore it, so their coordinates are multiplied
 * by f by hand.
 */
static void
draw_scene (LrgImageCanvas *canvas,
            GrlImageFont   *font,
            gfloat          size,
            gint            f)
{
	g_autoptr(GrlPath) star = grl_path_new ();
	GrlColor           a;
	GrlColor           b;
	GrlVector2        *m;
	gfloat             u = size / 640.0f;
	gfloat             cx = size * 0.5f;
	gint               i;

	/* --- sky: two gradients (large pixels) --- */
	set_color (&a, 30, 26, 70, 255);
	set_color (&b, 6, 6, 16, 255);
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) size * f, (gint) size * f, &a, &b,
	                                     GRL_GRADIENT_AXIS_VERTICAL);
	set_color (&a, 255, 80, 180, 36);
	set_color (&b, 255, 80, 180, 0);
	lrg_image_canvas_draw_gradient_radial (canvas, (gint) cx * f, (gint) (size * 0.2f) * f,
	                                       (gint) (size * 0.45f) * f, &a, &b);

	/* --- the glow title, centred (large pixels) --- */
	if (font != NULL)
	{
		gfloat fs = 76.0f * u;

		m = grl_image_measure_text_ttf (font, "GLOW", fs);
		set_color (&a, 255, 120, 220, 255);
		lrg_image_canvas_draw_text (canvas, font, "GLOW", (gint) ((cx - m->x * 0.5f) * f),
		                            (gint) (48.0f * u * f), fs * f, &a);
		grl_vector2_free (m);
	}

	/* --- geometry in final coordinates --- */
	lrg_image_canvas_save (canvas);
	lrg_image_canvas_scale (canvas, (gfloat) f, (gfloat) f);

	/* three translucent discs: overlaps show the blend mode */
	set_color (&a, 255, 70, 90, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) (cx - 70 * u), (gint) (300 * u), (gint) (90 * u), &a);
	set_color (&a, 60, 220, 140, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) cx, (gint) (400 * u), (gint) (90 * u), &a);
	set_color (&a, 70, 140, 255, 150);
	lrg_image_canvas_fill_circle (canvas, (gint) (cx + 70 * u), (gint) (300 * u), (gint) (90 * u), &a);

	/* a star outline-free polygon path */
	for (i = 0; i < 10; i++)
	{
		gfloat ang = -G_PI * 0.5 + i * G_PI / 5.0;
		gfloat r = (i % 2 == 0 ? 46.0f : 19.0f) * u;
		gfloat px = size * 0.84f + cosf (ang) * r;
		gfloat py = size * 0.56f + sinf (ang) * r;

		if (i == 0)
			grl_path_move_to (star, px, py);
		else
			grl_path_line_to (star, px, py);
	}
	grl_path_close (star);
	set_color (&a, 255, 205, 80, 255);
	lrg_image_canvas_fill_path (canvas, star, GRL_FILL_RULE_NONZERO, &a);

	/* thin lines: a fan and rings */
	for (i = 0; i < 30; i++)
	{
		gfloat t = i / 29.0f;

		set_color (&a, (guint8) (110 + 145 * t), (guint8) (220 - 100 * t), 255, 220);
		lrg_image_canvas_draw_line (canvas, (gint) (40 * u), (gint) (565 * u),
		                            (gint) ((40 + 560 * t) * u), (gint) ((490 + 70 * t) * u), 1, &a);
	}
	for (i = 0; i < 6; i++)
	{
		set_color (&a, 255, 255, 255, (guint8) (200 - 25 * i));
		lrg_image_canvas_stroke_circle (canvas, (gint) (size * 0.16f), (gint) (size * 0.56f),
		                                (gint) ((22 + 7 * i) * u), 1, &a);
	}
	lrg_image_canvas_restore (canvas);
}

/* bloom, ACES, vignette and grain on the finished 8-bit image */
static void
apply_post (GrlImage *image)
{
	g_autoptr(LrgPostProcessor) post = lrg_post_processor_new (1, 1);
	g_autoptr(LrgBloom)         bloom = lrg_bloom_new ();
	g_autoptr(LrgTonemap)       tonemap = lrg_tonemap_new ();
	g_autoptr(LrgVignette)      vignette = lrg_vignette_new ();
	g_autoptr(LrgFilmGrain)     grain = lrg_film_grain_new ();
	g_autoptr(GError)           error = NULL;

	lrg_bloom_set_threshold (bloom, 0.5f);
	lrg_bloom_set_intensity (bloom, 1.2f);
	lrg_bloom_set_iterations (bloom, 6);
	lrg_tonemap_set_operator (tonemap, LRG_TONEMAP_OPERATOR_ACES);
	lrg_tonemap_set_exposure (tonemap, 0.3f);
	lrg_vignette_set_intensity (vignette, 0.5f);
	lrg_vignette_set_smoothness (vignette, 0.6f);
	lrg_film_grain_set_intensity (grain, 0.05f);
	lrg_film_grain_set_size (grain, 1.0f);

	/* the order is the priority order */
	lrg_post_effect_set_priority (LRG_POST_EFFECT (bloom), 10);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (tonemap), 20);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (vignette), 30);
	lrg_post_effect_set_priority (LRG_POST_EFFECT (grain), 40);
	lrg_post_processor_add_effect (post, LRG_POST_EFFECT (bloom));
	lrg_post_processor_add_effect (post, LRG_POST_EFFECT (tonemap));
	lrg_post_processor_add_effect (post, LRG_POST_EFFECT (vignette));
	lrg_post_processor_add_effect (post, LRG_POST_EFFECT (grain));
	if (!lrg_post_processor_apply_to_image (post, image, 0.0f, &error))
		g_printerr ("post: %s\n", error->message);
}

/* a dark pill with a caption, centred at the top of a half */
static void
draw_label (LrgImageCanvas *canvas,
            GrlImageFont   *font,
            const gchar    *text,
            gfloat          cx,
            gfloat          size)
{
	GrlColor    pill;
	GrlColor    ink;
	GrlVector2 *m;
	gfloat      fs = size / 640.0f * 18.0f;
	gfloat      w;

	set_color (&pill, 0, 0, 0, 170);
	set_color (&ink, 250, 250, 250, 255);
	if (font == NULL)
		return;
	m = grl_image_measure_text_ttf (font, text, fs);
	w = m->x + fs * 1.4f;
	lrg_image_canvas_fill_rect (canvas, (gint) (cx - w * 0.5f), (gint) (size * 0.925f),
	                            (gint) w, (gint) (fs * 1.8f), &pill);
	lrg_image_canvas_draw_text (canvas, font, text, (gint) (cx - m->x * 0.5f),
	                            (gint) (size * 0.925f + fs * 0.35f), fs, &ink);
	grl_vector2_free (m);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GrlImageFont)   font = NULL;
	g_autoptr(LrgImageCanvas) plain = NULL;
	g_autoptr(LrgImageCanvas) quality = NULL;
	g_autoptr(LrgImageCanvas) sheet = NULL;
	g_autoptr(GrlImage)       resolved = NULL;
	g_autofree gchar         *path = NULL;
	GrlColor                  bg = { 6, 6, 16, 255 };
	guint                     width = 1280;
	guint                     height = 640;
	gfloat                    half;
	gchar                     tail;

	ctx = g_option_context_new ("- the same scene with default and quality settings");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Writes image-quality-showcase.png: left half drawn with lrg_image_canvas_new(),\n"
		"right half with the quality preset, 3x supersampling and the CPU post stack.\n"
		"\n"
		"Examples:\n"
		"  image-quality-showcase                           # ./image-quality-showcase.png\n"
		"  image-quality-showcase --out shots --size 1920x960\n"
		"  image-quality-showcase --font /usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("image-quality-showcase - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (opt_size != NULL &&
	    (sscanf (opt_size, "%ux%u%c", &width, &height, &tail) != 2 || width < 64 || height < 32 ||
	     width > 8192 || height > 8192))
	{
		g_printerr ("--size: expected WxH (64x32 .. 8192x8192), got \"%s\"\n", opt_size);
		return 2;
	}
	SetTraceLogLevel (LOG_WARNING);

	/* the font is optional: without it the halves have no text */
	font = grl_image_font_new_from_file (opt_font != NULL ? opt_font : FONT_DEFAULT, NULL);
	if (font == NULL)
		g_printerr ("warning: no font at %s, drawing without text\n",
		            opt_font != NULL ? opt_font : FONT_DEFAULT);
	half = (gfloat) (width / 2);

	/* --- left: the defaults, exactly what a plain canvas does --- */
	plain = lrg_image_canvas_new ((gint) half, (gint) height, &bg);
	draw_scene (plain, font, MIN (half, (gfloat) height), 1);

	/* --- right: quality preset at 3x, resolve, post stack --- */
	quality = lrg_image_canvas_new_supersampled ((gint) half, (gint) height, 3, &bg);
	draw_scene (quality, font, MIN (half, (gfloat) height), 3);
	resolved = lrg_image_canvas_resolve (quality, LRG_RESAMPLE_FILTER_LANCZOS3);
	apply_post (resolved);

	/* --- side by side, labelled --- */
	sheet = lrg_image_canvas_new_quality ((gint) width, (gint) height, &bg);
	lrg_image_canvas_composite (sheet, lrg_image_canvas_get_image (plain), GRL_PORTER_DUFF_SRC, 0, 0);
	lrg_image_canvas_composite (sheet, resolved, GRL_PORTER_DUFF_SRC, (gint) half, 0);
	draw_label (sheet, font, "defaults", half * 0.5f, MIN (half, (gfloat) height));
	draw_label (sheet, font, "quality preset + supersample + post", half * 1.5f,
	            MIN (half, (gfloat) height));

	if (opt_out != NULL && g_mkdir_with_parents (opt_out, 0755) != 0)
	{
		g_printerr ("--out: cannot create %s\n", opt_out);
		return 1;
	}
	path = g_build_filename (opt_out != NULL ? opt_out : ".", "image-quality-showcase.png", NULL);
	if (!grl_image_export (lrg_image_canvas_get_image (sheet), path))
	{
		g_printerr ("could not write %s\n", path);
		return 1;
	}
	g_print ("%s\n", path);
	return 0;
}
