/* frame-layout-poster.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A poster laid out with LrgFrameLayout: a full-bleed backdrop, a
 * title, a character, its speech bubble and a footer, placed against a
 * 5% safe area (anchored and relative boxes), checked with
 * lrg_frame_layout_validate () before a pixel is drawn, then drawn
 * into the boxes on a 3x supersampled quality canvas with anti-aliased
 * SDF fills and fitted text.  --debug-boxes draws the boxes on top and
 * prints the check; --broken pushes the bubble into the title to show
 * a failing check.  Exits 1 when the layout does not validate.
 * Headless: no window, no GL context.
 *
 * Run:   ./build/release/examples/frame-layout-poster
 *        ./build/release/examples/frame-layout-poster --debug-boxes --out shots
 *        ./build/release/examples/frame-layout-poster --broken --debug-boxes
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libregnum.h>
#include <raylib.h>

static gchar    *opt_out = NULL;
static gchar    *opt_size = NULL;
static gchar    *opt_font = NULL;
static gboolean  opt_debug = FALSE;
static gboolean  opt_broken = FALSE;
static gboolean  opt_license = FALSE;

static GOptionEntry entries[] = {
	{ "out", 'o', 0, G_OPTION_ARG_FILENAME, &opt_out, "Folder for the PNG (default: current folder)", "DIR" },
	{ "size", 's', 0, G_OPTION_ARG_STRING, &opt_size, "Poster size (default: 960x540)", "WxH" },
	{ "font", 'f', 0, G_OPTION_ARG_FILENAME, &opt_font, "TrueType font (default: DejaVu Sans Bold)", "FILE" },
	{ "debug-boxes", 'd', 0, G_OPTION_ARG_NONE, &opt_debug,
	  "Draw the layout boxes on top (frame-layout-poster-debug.png) and print the check", NULL },
	{ "broken", 0, 0, G_OPTION_ARG_NONE, &opt_broken, "Push the bubble into the title (the check fails)", NULL },
	{ "license", 0, 0, G_OPTION_ARG_NONE, &opt_license, "Print the license and exit", NULL },
	{ NULL }
};

#define FONT_DEFAULT "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"
#define SS (3)

/* ==========================================================================
 * Anti-aliased shapes: one SDF callback for every shape, in final pixels
 * ========================================================================== */

typedef enum
{
	SHAPE_CIRCLE,
	SHAPE_ELLIPSE,
	SHAPE_ROUND_BOX,
	SHAPE_CAPSULE,
	SHAPE_TRIANGLE
} ShapeKind;

typedef struct
{
	ShapeKind kind;
	gfloat    a[2];
	gfloat    b[2];
	gfloat    c[2];
	gfloat    r;
	gfloat    feather;   /* edge width in final pixels; 1 = crisp */
} Shape;

static gfloat
shape_sdf (const gfloat p[2],
           gpointer     user_data)
{
	const Shape *s = user_data;
	gfloat       q[2];
	gfloat       l[2];
	gfloat       d;

	/* large (supersampled) pixel -> final pixel */
	q[0] = p[0] / SS;
	q[1] = p[1] / SS;
	l[0] = q[0] - s->a[0];
	l[1] = q[1] - s->a[1];
	switch (s->kind)
	{
	case SHAPE_CIRCLE:    d = lrg_sdf2_circle (l, s->r); break;
	case SHAPE_ELLIPSE:   d = lrg_sdf2_ellipse (l, s->b); break;
	case SHAPE_ROUND_BOX: d = lrg_sdf2_round_box (l, s->b, s->r); break;
	case SHAPE_CAPSULE:   d = lrg_sdf2_capsule (q, s->a, s->b, s->r); break;
	case SHAPE_TRIANGLE:  d = lrg_sdf2_triangle (q, s->a, s->b, s->c); break;
	default:              d = 1e9f; break;
	}

	/* back to large pixels; the feather widens the edge ramp */
	return d * SS / MAX (s->feather, 1e-3f);
}

/* fill a shape inside the given bounds (final pixels) */
static void
fill (LrgImageCanvas *canvas,
      Shape          *s,
      gfloat          x0,
      gfloat          y0,
      gfloat          x1,
      gfloat          y1,
      guint8          r,
      guint8          g,
      guint8          b,
      guint8          a)
{
	GrlColor c = { r, g, b, a };
	gfloat   pad;

	if (s->feather <= 0.0f)
		s->feather = 1.0f;
	pad = s->feather + 2.0f;
	lrg_image_canvas_fill_sdf_rect (canvas, (gint) ((x0 - pad) * SS), (gint) ((y0 - pad) * SS),
	                                (gint) ((x1 - x0 + 2 * pad) * SS), (gint) ((y1 - y0 + 2 * pad) * SS),
	                                shape_sdf, s, &c);
}

static void
circle (LrgImageCanvas *canvas, gfloat cx, gfloat cy, gfloat rad, gfloat feather,
        guint8 r, guint8 g, guint8 b, guint8 a)
{
	Shape s;

	memset (&s, 0, sizeof (s));
	s.kind = SHAPE_CIRCLE;
	s.a[0] = cx;
	s.a[1] = cy;
	s.r = rad;
	s.feather = feather;
	fill (canvas, &s, cx - rad, cy - rad, cx + rad, cy + rad, r, g, b, a);
}

static void
ellipse (LrgImageCanvas *canvas, gfloat cx, gfloat cy, gfloat rx, gfloat ry, gfloat feather,
         guint8 r, guint8 g, guint8 b, guint8 a)
{
	Shape s;

	memset (&s, 0, sizeof (s));
	s.kind = SHAPE_ELLIPSE;
	s.a[0] = cx;
	s.a[1] = cy;
	s.b[0] = rx;
	s.b[1] = ry;
	s.feather = feather;
	fill (canvas, &s, cx - rx, cy - ry, cx + rx, cy + ry, r, g, b, a);
}

static void
round_box (LrgImageCanvas *canvas, gfloat x, gfloat y, gfloat w, gfloat h, gfloat rad, gfloat feather,
           guint8 r, guint8 g, guint8 b, guint8 a)
{
	Shape s;

	memset (&s, 0, sizeof (s));
	s.kind = SHAPE_ROUND_BOX;
	s.a[0] = x + w * 0.5f;
	s.a[1] = y + h * 0.5f;
	s.b[0] = w * 0.5f;
	s.b[1] = h * 0.5f;
	s.r = rad;
	s.feather = feather;
	fill (canvas, &s, x, y, x + w, y + h, r, g, b, a);
}

static void
capsule (LrgImageCanvas *canvas, gfloat ax, gfloat ay, gfloat bx, gfloat by, gfloat rad,
         guint8 r, guint8 g, guint8 b, guint8 a)
{
	Shape s;

	memset (&s, 0, sizeof (s));
	s.kind = SHAPE_CAPSULE;
	s.a[0] = ax;
	s.a[1] = ay;
	s.b[0] = bx;
	s.b[1] = by;
	s.r = rad;
	s.feather = 1.0f;
	fill (canvas, &s, MIN (ax, bx) - rad, MIN (ay, by) - rad, MAX (ax, bx) + rad, MAX (ay, by) + rad,
	      r, g, b, a);
}

/* ==========================================================================
 * Text: fitted into a box and centred
 * ========================================================================== */

static void
text_in_box (LrgImageCanvas     *canvas,
             GrlImageFont       *font,
             const gchar        *text,
             const GrlRectangle *box,
             guint8              r,
             guint8              g,
             guint8              b)
{
	GrlColor    c = { r, g, b, 255 };
	GrlVector2 *m;
	gfloat      size;
	gfloat      w;
	gfloat      h;

	if (font == NULL)
		return;

	/* measurements scale with the size: measure at 100 px, scale to fit */
	m = grl_image_measure_text_ttf (font, text, 100.0f);
	size = MIN (100.0f * box->width / MAX (m->x, 1.0f), 100.0f * box->height / MAX (m->y, 1.0f));
	grl_vector2_free (m);
	m = grl_image_measure_text_ttf (font, text, size);
	w = m->x;
	h = m->y;
	grl_vector2_free (m);

	/* text ignores the transform stack: large pixels */
	lrg_image_canvas_draw_text (canvas, font, text,
	                            (gint) ((box->x + (box->width - w) * 0.5f) * SS),
	                            (gint) ((box->y + (box->height - h) * 0.5f) * SS), size * SS, &c);
}

/* ==========================================================================
 * The poster
 * ========================================================================== */

/*
 * build_layout:
 * Every element gets a named box: the backdrop covers the frame and
 * lies under everything; the rest stays inside the 5% safe area.
 */
static LrgFrameLayout *
build_layout (gfloat   w,
              gfloat   h,
              gboolean broken)
{
	LrgFrameLayout *layout = lrg_frame_layout_new (w, h);
	GrlRectangle    full = { 0.0f, 0.0f, w, h };

	lrg_frame_layout_set_safe_area_percent (layout, 0.05f);
	lrg_frame_layout_set_box_rect (layout, "backdrop", &full);
	lrg_frame_layout_set_backdrop (layout, "backdrop", TRUE);
	lrg_frame_layout_add_box (layout, "title", LRG_HUD_ANCHOR_TOP, 0.0f, 0.0f, w * 0.62f, h * 0.15f);
	lrg_frame_layout_add_box (layout, "character", LRG_HUD_ANCHOR_BOTTOM_LEFT, w * 0.06f, -h * 0.11f,
	                          w * 0.24f, h * 0.56f);
	lrg_frame_layout_add_box_relative (layout, "bubble", "character", LRG_FRAME_SIDE_RIGHT, w * 0.03f,
	                                   w * 0.42f, h * 0.30f, LRG_FRAME_ALIGN_START);
	lrg_frame_layout_add_box (layout, "footer", LRG_HUD_ANCHOR_BOTTOM, 0.0f, 0.0f, w * 0.80f, h * 0.07f);

	/* the deliberate mistake: the bubble sits over the title */
	if (broken)
	{
		GrlRectangle over = { w * 0.40f, h * 0.10f, w * 0.42f, h * 0.30f };

		lrg_frame_layout_set_box_rect (layout, "bubble", &over);
	}
	return layout;
}

static void
draw_character (LrgImageCanvas     *canvas,
                const GrlRectangle *box)
{
	gfloat cx = box->x + box->width * 0.5f;
	gfloat bw = box->width;
	gfloat bh = box->height;
	gfloat hy = box->y + bh * 0.30f;
	gfloat hr = MIN (bw * 0.32f, bh * 0.2f);
	gint   side;

	ellipse (canvas, cx, box->y + bh * 0.96f, bw * 0.40f, bh * 0.03f, 8.0f, 30, 10, 40, 120);
	round_box (canvas, cx - bw * 0.32f, box->y + bh * 0.48f, bw * 0.64f, bh * 0.48f, bw * 0.24f, 1.0f,
	           70, 130, 210, 255);
	ellipse (canvas, cx - bw * 0.1f, box->y + bh * 0.6f, bw * 0.1f, bh * 0.08f, bw * 0.12f, 170, 210, 255, 120);
	capsule (canvas, cx + bw * 0.24f, box->y + bh * 0.62f, box->x + bw * 0.94f, box->y + bh * 0.48f,
	         bw * 0.06f, 60, 115, 190, 255);
	circle (canvas, cx, hy, hr, 1.0f, 250, 214, 160, 255);
	circle (canvas, cx - hr * 0.35f, hy - hr * 0.4f, hr * 0.35f, hr * 0.45f, 255, 245, 220, 140);
	for (side = -1; side <= 1; side += 2)
	{
		gfloat ex = cx + side * hr * 0.38f;

		ellipse (canvas, ex, hy - hr * 0.08f, hr * 0.2f, hr * 0.26f, 1.0f, 255, 255, 255, 255);
		circle (canvas, ex + hr * 0.08f, hy - hr * 0.04f, hr * 0.11f, 1.0f, 30, 30, 50, 255);
		circle (canvas, ex + side * hr * 0.1f, hy + hr * 0.38f, hr * 0.15f, hr * 0.12f, 255, 120, 130, 110);
	}
	capsule (canvas, cx - hr * 0.22f, hy + hr * 0.45f, cx + hr * 0.24f, hy + hr * 0.4f, MAX (0.8f, hr * 0.06f),
	         120, 50, 40, 255);
}

static void
draw_bubble (LrgImageCanvas     *canvas,
             GrlImageFont       *font,
             const GrlRectangle *box)
{
	Shape        tail;
	GrlRectangle inner;
	gfloat       tw = box->width * 0.09f;
	gfloat       x0 = box->x + tw;
	gfloat       w = box->width - tw;
	gfloat       r = box->height * 0.22f;

	round_box (canvas, x0 + 4.0f, box->y + 6.0f, w - 6.0f, box->height - 8.0f, r, 10.0f, 20, 10, 40, 90);
	round_box (canvas, x0, box->y, w - 2.0f, box->height - 4.0f, r, 1.0f, 255, 253, 248, 255);
	memset (&tail, 0, sizeof (tail));
	tail.kind = SHAPE_TRIANGLE;
	tail.a[0] = box->x;
	tail.a[1] = box->y + box->height * 0.62f;
	tail.b[0] = x0 + r;
	tail.b[1] = box->y + box->height * 0.36f;
	tail.c[0] = x0 + r;
	tail.c[1] = box->y + box->height * 0.60f;
	tail.feather = 1.0f;
	fill (canvas, &tail, box->x, box->y, x0 + r, box->y + box->height, 255, 253, 248, 255);
	inner.x = x0 + r * 0.7f;
	inner.y = box->y + box->height * 0.18f;
	inner.width = w - r * 1.4f;
	inner.height = box->height * 0.58f;
	text_in_box (canvas, font, "Nothing overlaps.\nI checked.", &inner, 40, 30, 60);
}

static void
draw_poster (LrgImageCanvas *canvas,
             LrgFrameLayout *layout,
             GrlImageFont   *font,
             gfloat          w,
             gfloat          h)
{
	GrlRectangle title;
	GrlRectangle shadow;
	GrlRectangle character;
	GrlRectangle bubble;
	GrlRectangle footer;
	GrlColor     top = { 36, 22, 84, 255 };
	GrlColor     bottom = { 250, 140, 90, 255 };
	GrlColor     glow = { 255, 220, 150, 150 };
	GrlColor     clear = { 255, 220, 150, 0 };

	lrg_frame_layout_get_box (layout, "title", &title);
	lrg_frame_layout_get_box (layout, "character", &character);
	lrg_frame_layout_get_box (layout, "bubble", &bubble);
	lrg_frame_layout_get_box (layout, "footer", &footer);

	/* backdrop: dusk gradient, a sun glow, rolling hills (full bleed) */
	lrg_image_canvas_draw_gradient_rect (canvas, 0, 0, (gint) w * SS, (gint) h * SS, &top, &bottom,
	                                     GRL_GRADIENT_AXIS_VERTICAL);
	lrg_image_canvas_draw_gradient_radial (canvas, (gint) (w * 0.87f) * SS, (gint) (h * 0.68f) * SS,
	                                       (gint) (h * 0.55f) * SS, &glow, &clear);
	circle (canvas, w * 0.87f, h * 0.68f, h * 0.08f, 1.0f, 255, 236, 190, 255);
	ellipse (canvas, w * 0.75f, h * 1.25f, w * 0.65f, h * 0.48f, 1.0f, 70, 40, 90, 255);
	ellipse (canvas, w * 0.15f, h * 1.3f, w * 0.7f, h * 0.45f, 1.0f, 44, 26, 64, 255);

	/* the elements, each in its box */
	draw_character (canvas, &character);
	draw_bubble (canvas, font, &bubble);
	shadow = title;
	shadow.x += MAX (1.0f, h * 0.005f);
	shadow.y += MAX (1.0f, h * 0.007f);
	text_in_box (canvas, font, "Mind The Gap", &shadow, 20, 10, 50);
	text_in_box (canvas, font, "Mind The Gap", &title, 255, 244, 225);
	text_in_box (canvas, font, "LAID OUT WITH LrgFrameLayout  \xc2\xb7  CHECKED BEFORE DRAWING", &footer,
	             255, 230, 210);
}

int
main (int   argc,
      char *argv[])
{
	g_autoptr(GOptionContext) ctx = NULL;
	g_autoptr(GError)         error = NULL;
	g_autoptr(GError)         check = NULL;
	g_autoptr(GrlImageFont)   font = NULL;
	g_autoptr(LrgFrameLayout) layout = NULL;
	g_autoptr(LrgImageCanvas) canvas = NULL;
	g_autoptr(GrlImage)       image = NULL;
	g_autofree gchar         *path = NULL;
	GrlColor                  bg = { 0, 0, 0, 255 };
	const gchar              *dir;
	guint                     width = 960;
	guint                     height = 540;
	gboolean                  valid;
	gchar                     tail;

	ctx = g_option_context_new ("- a poster laid out and checked with LrgFrameLayout");
	g_option_context_add_main_entries (ctx, entries, NULL);
	g_option_context_set_description (ctx,
		"Writes frame-layout-poster.png (and frame-layout-poster-debug.png with\n"
		"--debug-boxes).  Exits 1 when the layout does not validate.\n"
		"\n"
		"Examples:\n"
		"  frame-layout-poster                              # ./frame-layout-poster.png\n"
		"  frame-layout-poster --debug-boxes --out shots    # also the box overlay\n"
		"  frame-layout-poster --broken --debug-boxes       # see a failing check\n"
		"  frame-layout-poster --size 1920x1080\n");
	if (!g_option_context_parse (ctx, &argc, &argv, &error))
	{
		g_printerr ("%s\n", error->message);
		return 2;
	}
	if (opt_license)
	{
		g_print ("frame-layout-poster - part of libregnum\n"
		         "Copyright 2026 Zach Podbielniak\n"
		         "License: AGPL-3.0-or-later <https://www.gnu.org/licenses/agpl-3.0.html>\n");
		return 0;
	}
	if (opt_size != NULL &&
	    (sscanf (opt_size, "%ux%u%c", &width, &height, &tail) != 2 || width < 160 || height < 90 ||
	     width > 8192 || height > 8192))
	{
		g_printerr ("--size: expected WxH (160x90 .. 8192x8192), got \"%s\"\n", opt_size);
		return 2;
	}
	SetTraceLogLevel (LOG_WARNING);
	font = grl_image_font_new_from_file (opt_font != NULL ? opt_font : FONT_DEFAULT, NULL);
	if (font == NULL)
		g_printerr ("warning: no font at %s, drawing without text\n",
		            opt_font != NULL ? opt_font : FONT_DEFAULT);

	/* --- 1. boxes, then the check --- */
	layout = build_layout ((gfloat) width, (gfloat) height, opt_broken);
	valid = lrg_frame_layout_validate (layout, &check);
	if (opt_debug)
		g_print ("layout check: %s\n", valid ? "ok (no overlaps, all inside the safe area)" : check->message);
	else if (!valid)
		g_printerr ("layout problems:\n%s\n", check->message);

	/* --- 2. pixels: 3x supersampled quality canvas, resolved --- */
	canvas = lrg_image_canvas_new_supersampled ((gint) width, (gint) height, SS, &bg);
	draw_poster (canvas, layout, font, (gfloat) width, (gfloat) height);
	image = lrg_image_canvas_resolve (canvas, LRG_RESAMPLE_FILTER_LANCZOS3);

	dir = opt_out != NULL ? opt_out : ".";
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_printerr ("--out: cannot create %s\n", dir);
		return 1;
	}
	path = g_build_filename (dir, "frame-layout-poster.png", NULL);
	if (image == NULL || !grl_image_export (image, path))
	{
		g_printerr ("could not write %s\n", path);
		return 1;
	}
	g_print ("%s\n", path);

	/* --- 3. the overlay, on a copy --- */
	if (opt_debug)
	{
		g_autoptr(LrgImageCanvas) overlay = lrg_image_canvas_new_for_image (image);
		g_autofree gchar         *debug_path = g_build_filename (dir, "frame-layout-poster-debug.png", NULL);

		lrg_image_canvas_set_quality (overlay);
		lrg_frame_layout_draw_debug (layout, overlay);
		if (!grl_image_export (image, debug_path))
		{
			g_printerr ("could not write %s\n", debug_path);
			return 1;
		}
		g_print ("%s\n", debug_path);
	}
	return valid ? 0 : 1;
}
