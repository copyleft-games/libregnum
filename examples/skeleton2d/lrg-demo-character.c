/* lrg-demo-character.c - a cartoon head-and-shoulders character built in code
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The character is five mesh slots on three bones:
 *
 *   body   on "body": shirt and neck (squash, stretch, breathe)
 *   head   on "head": face, ears, hair, cheeks, nose (squash, stretch)
 *   eyes   on "head": per eye a white, an iris, an upper and a lower lid
 *   brows  on "head": two brows
 *   mouth  on "head": a lens-shaped mouth with teeth and tongue bands
 *
 * Every texture is painted into a GrlImage here (supersampled shapes,
 * source-over), set as the sprite's CPU source image, so the character
 * renders headless with lrg_skeleton2d_draw_to_canvas().
 *
 * The shapes are computed, not hand-typed: for each shape a small pose
 * description (how far an eyelid comes down, how wide the mouth opens,
 * ...) gives every vertex a target position, and the shape's offsets are
 * target minus rest.  The tricks that make blends read well:
 *
 * - Eyelids are skin-coloured quads above (upper) and below (lower) each
 *   eye, drawn over the white and the iris.  The upper lid's texture is
 *   skin above the lower half of an oval with a lash line along it, so a
 *   half-closed lid has a curved edge and a closed one covers the eye
 *   with a "smiling" lash arc.  At rest both lids are folded to no
 *   height at the top and bottom of the eye.
 * - The mouth mesh has four vertex rows: top edge, teeth bottom, tongue
 *   top, bottom edge, mapped to the texture's teeth, dark interior and
 *   tongue bands.  A viseme moves the rows apart by different amounts,
 *   so "closed" is a dark line (teeth and tongue bands collapsed) and
 *   "A" shows teeth, interior and tongue.  Shapes add, so a mood's
 *   smile curve composes with any viseme.
 * - Squash and stretch scale every slot about the feet, so the whole
 *   character keeps together.
 */

#include "lrg-demo-character.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Layout (setup space: y down, origin at the bottom of the shirt)      */
/* ------------------------------------------------------------------ */

#define FACE_RX         (120.0f)
#define FACE_RY         (132.0f)
#define NECK_Y          (-180.0f)

#define EYE_X           (46.0f)    /* eye centres at -EYE_X and +EYE_X */
#define EYE_Y           (-312.0f)
#define EYE_RX          (26.0f)    /* the white, without its outline */
#define EYE_RY          (32.0f)
#define EYE_CELL_W      (60)       /* white, upper lid and lower lid cells */
#define EYE_CELL_H      (72)
#define IRIS_CELL       (36)
#define IRIS_DROP       (3.0f)     /* irises sit a little low: friendlier */
#define LID_REST        (0.0f)     /* upper lid height at rest (folded away) */
#define LID_CLOSE       (72.0f)    /* lid travel that closes the eye */

#define BROW_X          (48.0f)
#define BROW_Y          (-370.0f)
#define BROW_W          (64)
#define BROW_H          (20)

#define MOUTH_Y         (-232.0f)
#define MOUTH_HALF_W    (48.0f)
#define MOUTH_COLS      (9)        /* vertex columns */
#define MOUTH_ROWS      (4)        /* top, teeth bottom, tongue top, bottom */
#define MOUTH_SMILE     (5.0f)     /* corners this much above the centre */
#define MOUTH_REST_H    (2.5f)     /* half the closed line's thickness */
#define MOUTH_TEX       (64)
#define MOUTH_TEETH_V   (0.30f)    /* texture band boundaries */
#define MOUTH_TONGUE_V  (0.70f)

#define HEAD_X0         (-160.0f)  /* head texture rectangle */
#define HEAD_Y0         (-450.0f)
#define HEAD_W          (320)
#define HEAD_H          (300)

#define BODY_X0         (-140.0f)  /* body texture rectangle */
#define BODY_Y0         (-190.0f)
#define BODY_W          (280)
#define BODY_H          (200)

/* eyes atlas: white | iris | upper lid | lower lid, 4 px apart */
#define ATLAS_WHITE_X   (0)
#define ATLAS_IRIS_X    (64)
#define ATLAS_UPPER_X   (104)
#define ATLAS_LOWER_X   (168)
#define ATLAS_W         (228)
#define ATLAS_H         (72)

/* palette */
static const GrlColor COL_CLEAR    = {   0,   0,   0,   0 };
static const GrlColor COL_SKIN     = { 255, 214, 176, 255 };
static const GrlColor COL_SKIN_DK  = { 236, 172, 136, 255 };
static const GrlColor COL_LINE     = {  92,  58,  44, 255 };
static const GrlColor COL_HAIR     = { 104,  64,  44, 255 };
static const GrlColor COL_HAIR_HI  = { 150,  98,  66, 255 };
static const GrlColor COL_BLUSH    = { 255, 128, 128,  90 };
static const GrlColor COL_WHITE    = { 255, 255, 255, 255 };
static const GrlColor COL_IRIS     = {  66, 132, 196, 255 };
static const GrlColor COL_IRIS_DK  = {  36,  76, 130, 255 };
static const GrlColor COL_PUPIL    = {  24,  20,  28, 255 };
static const GrlColor COL_LASH     = {  60,  36,  30, 255 };
static const GrlColor COL_BROW     = {  86,  52,  36, 255 };
static const GrlColor COL_TEETH    = { 250, 250, 244, 255 };
static const GrlColor COL_MOUTH    = {  96,  28,  40, 255 };
static const GrlColor COL_MOUTH_DK = {  70,  18,  30, 255 };
static const GrlColor COL_TONGUE   = { 232, 104, 116, 255 };
static const GrlColor COL_SHIRT    = {  88, 164, 220, 255 };
static const GrlColor COL_SHIRT_DK = {  44, 100, 156, 255 };
static const GrlColor COL_SHIRT_HI = { 132, 194, 238, 255 };

/* ------------------------------------------------------------------ */
/* Painting: supersampled implicit shapes, source-over                 */
/* ------------------------------------------------------------------ */

/* TRUE when the texture-space point (x, y) is inside the shape */
typedef gboolean (*InsideFunc) (gfloat        x,
                                gfloat        y,
                                gconstpointer data);

/* Blends @c at coverage @cov over the straight-alpha RGBA8 pixel @d. */
static void
blend_pixel (guint8         *d,
             const GrlColor *c,
             gfloat          cov)
{
	gfloat sa = ((gfloat)c->a / 255.0f) * cov;
	gfloat da = (gfloat)d[3] / 255.0f;
	gfloat oa;
	gfloat k;

	if (sa <= 0.0f)
		return;
	oa = sa + da * (1.0f - sa);
	k = da * (1.0f - sa);
	d[0] = (guint8)(((gfloat)c->r * sa + (gfloat)d[0] * k) / oa + 0.5f);
	d[1] = (guint8)(((gfloat)c->g * sa + (gfloat)d[1] * k) / oa + 0.5f);
	d[2] = (guint8)(((gfloat)c->b * sa + (gfloat)d[2] * k) / oa + 0.5f);
	d[3] = (guint8)(oa * 255.0f + 0.5f);
}

/*
 * Paints the shape @inside describes in colour @c over the pixels of
 * @img inside the box (x0, y0) .. (x1, y1).  Each pixel's coverage is
 * the fraction of a 4 x 4 grid of sample points inside the shape, so
 * edges come out anti-aliased.
 */
static void
paint (GrlImage       *img,
       gfloat          x0,
       gfloat          y0,
       gfloat          x1,
       gfloat          y1,
       InsideFunc      inside,
       gconstpointer   data,
       const GrlColor *c)
{
	guint8 *px = grl_image_get_pixels (img, NULL);
	gint    w = grl_image_get_width (img);
	gint    h = grl_image_get_height (img);
	gint    ix0 = MAX ((gint)floorf (x0), 0);
	gint    iy0 = MAX ((gint)floorf (y0), 0);
	gint    ix1 = MIN ((gint)ceilf (x1), w);
	gint    iy1 = MIN ((gint)ceilf (y1), h);
	gint    x;
	gint    y;

	if (px == NULL)
		return;
	for (y = iy0; y < iy1; y++)
	{
		for (x = ix0; x < ix1; x++)
		{
			gint n = 0;
			gint i;
			gint j;

			for (j = 0; j < 4; j++)
				for (i = 0; i < 4; i++)
					if (inside ((gfloat)x + ((gfloat)i + 0.5f) / 4.0f,
					            (gfloat)y + ((gfloat)j + 0.5f) / 4.0f, data))
						n++;
			if (n > 0)
				blend_pixel (px + ((gsize)y * (gsize)w + (gsize)x) * 4, c, (gfloat)n / 16.0f);
		}
	}
}

/* An axis-aligned ellipse; with @half set only the half below (1) or
 * above (-1) its centre. */
typedef struct
{
	gfloat cx;
	gfloat cy;
	gfloat rx;
	gfloat ry;
	gint   half;
} Ellipse;

static gboolean
inside_ellipse (gfloat        x,
                gfloat        y,
                gconstpointer data)
{
	const Ellipse *e = data;
	gfloat         dx = (x - e->cx) / e->rx;
	gfloat         dy = (y - e->cy) / e->ry;

	if (e->half > 0 && y < e->cy)
		return FALSE;
	if (e->half < 0 && y > e->cy)
		return FALSE;
	return dx * dx + dy * dy <= 1.0f;
}

static void
paint_ellipse (GrlImage       *img,
               gfloat          cx,
               gfloat          cy,
               gfloat          rx,
               gfloat          ry,
               const GrlColor *c)
{
	Ellipse e;

	e.cx = cx;
	e.cy = cy;
	e.rx = rx;
	e.ry = ry;
	e.half = 0;
	paint (img, cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1, inside_ellipse, &e, c);
}

/* A ring of thickness 2 * t around an ellipse, optionally one half. */
typedef struct
{
	Ellipse e;
	gfloat  t;
} Ring;

static gboolean
inside_ring (gfloat        x,
             gfloat        y,
             gconstpointer data)
{
	const Ring *r = data;
	gfloat      dxo = (x - r->e.cx) / (r->e.rx + r->t);
	gfloat      dyo = (y - r->e.cy) / (r->e.ry + r->t);
	gfloat      dxi = (x - r->e.cx) / (r->e.rx - r->t);
	gfloat      dyi = (y - r->e.cy) / (r->e.ry - r->t);

	if (r->e.half > 0 && y < r->e.cy)
		return FALSE;
	if (r->e.half < 0 && y > r->e.cy)
		return FALSE;
	return dxo * dxo + dyo * dyo <= 1.0f && dxi * dxi + dyi * dyi > 1.0f;
}

/* A thick polyline with round joins and ends whose radius tapers from
 * @r_mid in the middle to @r_end at both ends. */
typedef struct
{
	const gfloat *pts;    /* x, y pairs */
	guint         n;
	gfloat        r_mid;
	gfloat        r_end;
} Stroke;

static gboolean
inside_stroke (gfloat        x,
               gfloat        y,
               gconstpointer data)
{
	const Stroke *s = data;
	guint         i;

	for (i = 0; i + 1 < s->n; i++)
	{
		gfloat ax = s->pts[i * 2];
		gfloat ay = s->pts[i * 2 + 1];
		gfloat bx = s->pts[i * 2 + 2];
		gfloat by = s->pts[i * 2 + 3];
		gfloat dx = bx - ax;
		gfloat dy = by - ay;
		gfloat len2 = dx * dx + dy * dy;
		gfloat t = len2 > 0.0f ? CLAMP (((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
		gfloat px = ax + t * dx - x;
		gfloat py = ay + t * dy - y;
		gfloat along = ((gfloat)i + t) / (gfloat)(s->n - 1);   /* 0 .. 1 over the polyline */
		gfloat m = 1.0f - fabsf (along * 2.0f - 1.0f);         /* 1 in the middle */
		gfloat r = s->r_end + (s->r_mid - s->r_end) * sqrtf (m);

		if (px * px + py * py <= r * r)
			return TRUE;
	}
	return FALSE;
}

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */

static GrlImage *
new_clear_image (gint w,
                 gint h)
{
	return grl_image_new_color (w, h, &COL_CLEAR);
}

/* The hair: inside a slightly larger head oval, above a scalloped
 * fringe, plus side locks over the temples.  Texture space of the head
 * image (face centre at (160, 150)). */
static gboolean
inside_hair (gfloat        x,
             gfloat        y,
             gconstpointer data)
{
	gfloat dx = (x - 160.0f) / (FACE_RX + 6.0f);
	gfloat dy = (y - 150.0f) / (FACE_RY + 8.0f);
	gfloat fringe;

	if (dx * dx + dy * dy > 1.0f)
		return FALSE;
	/* scallops: three rounded locks across the forehead */
	fringe = 62.0f + 12.0f * fabsf (sinf ((x - 160.0f) / 26.0f + 0.4f));
	if (y < fringe)
		return TRUE;
	/* side locks down past the ear tops, their inner edge an oval so
	 * they thicken smoothly into the fringe */
	if (y >= 136.0f)
		return FALSE;
	dx = (x - 160.0f) / 98.0f;
	dy = (y - 176.0f) / 140.0f;
	return dx * dx + dy * dy > 1.0f;
}

/* face, ears, hair, cheeks and nose on a 320 x 300 image covering
 * setup (-160, -450) .. (160, -150) */
static GrlImage *
paint_head (void)
{
	GrlImage *img = new_clear_image (HEAD_W, HEAD_H);
	gfloat    fx = -HEAD_X0;                 /* face centre in the image */
	gfloat    fy = LRG_DEMO_FACE_Y - HEAD_Y0;
	gint      side;

	/* ears behind the face: outline, skin, inner ear */
	for (side = -1; side <= 1; side += 2)
	{
		gfloat ex = fx + (gfloat)side * (FACE_RX - 4.0f);

		paint_ellipse (img, ex, fy + 4.0f, 27.0f, 31.0f, &COL_LINE);
		paint_ellipse (img, ex, fy + 4.0f, 24.0f, 28.0f, &COL_SKIN);
		paint_ellipse (img, ex + (gfloat)side * 6.0f, fy + 6.0f, 11.0f, 15.0f, &COL_SKIN_DK);
	}

	/* the face oval with a 3 px outline */
	paint_ellipse (img, fx, fy, FACE_RX + 3.0f, FACE_RY + 3.0f, &COL_LINE);
	paint_ellipse (img, fx, fy, FACE_RX, FACE_RY, &COL_SKIN);

	/* hair with a highlight lock */
	paint (img, 0, 0, HEAD_W, HEAD_H, inside_hair, NULL, &COL_HAIR);
	{
		gfloat pts[8] = { fx - 50.0f, fy - 112.0f, fx - 20.0f, fy - 122.0f, fx + 15.0f, fy - 124.0f, fx + 45.0f, fy - 116.0f };
		Stroke s;

		s.pts = pts;
		s.n = 4;
		s.r_mid = 4.0f;
		s.r_end = 1.5f;
		paint (img, fx - 60.0f, fy - 135.0f, fx + 60.0f, fy - 100.0f, inside_stroke, &s, &COL_HAIR_HI);
	}

	/* rosy cheeks and a button nose with a highlight */
	paint_ellipse (img, fx - 74.0f, fy + 40.0f, 21.0f, 13.0f, &COL_BLUSH);
	paint_ellipse (img, fx + 74.0f, fy + 40.0f, 21.0f, 13.0f, &COL_BLUSH);
	paint_ellipse (img, fx, fy + 18.0f, 11.0f, 8.0f, &COL_SKIN_DK);
	paint_ellipse (img, fx - 3.5f, fy + 15.0f, 3.5f, 2.5f, &COL_WHITE);
	return img;
}

/* The shirt outline: a flat-bottomed shape whose shoulders round off as
 * a superellipse; @m shrinks it by a margin.  Texture space of the body
 * image (setup x 0 at 140, setup y 0 at 190). */
static gboolean
inside_shirt (gfloat        x,
              gfloat        y,
              gconstpointer data)
{
	gfloat m = *(const gfloat *)data;
	gfloat sx = x - 140.0f;
	gfloat sy = y - 190.0f;                  /* setup y, 0 at the bottom */
	gfloat top = -165.0f + m;
	gfloat half;

	if (sy < top || sy > 0.0f)
		return FALSE;
	if (sy >= -105.0f)
		half = 120.0f + (sy + 105.0f) * 0.08f;
	else
	{
		gfloat t = fabsf ((sy + 105.0f) / (top + 105.0f));

		half = 120.0f * cbrtf (MAX (1.0f - t * t * t, 0.0f));
	}
	return fabsf (sx) <= half - m;
}

/* A V-neck opening, apex down. */
static gboolean
inside_vneck (gfloat        x,
              gfloat        y,
              gconstpointer data)
{
	gfloat m = *(const gfloat *)data;
	gfloat sx = fabsf (x - 140.0f);
	gfloat sy = y - 190.0f;

	if (sy < -170.0f || sy > -122.0f + m)
		return FALSE;
	return sx <= (-122.0f + m - sy) * 0.62f;
}

/* neck and shirt on a 280 x 200 image covering setup (-140, -190) ..
 * (140, 10) */
static GrlImage *
paint_body (void)
{
	GrlImage *img = new_clear_image (BODY_W, BODY_H);
	gfloat    zero = 0.0f;
	gfloat    inset = 3.0f;
	gfloat    plus = 3.0f;

	/* the neck, peeking out of the collar under the head */
	paint_ellipse (img, 140.0f, 190.0f - 168.0f, 34.0f, 34.0f, &COL_LINE);
	paint_ellipse (img, 140.0f, 190.0f - 168.0f, 31.0f, 31.0f, &COL_SKIN);

	/* shirt: outline, fill, a soft shoulder highlight */
	paint (img, 0, 0, BODY_W, BODY_H, inside_shirt, &zero, &COL_SHIRT_DK);
	paint (img, 0, 0, BODY_W, BODY_H, inside_shirt, &inset, &COL_SHIRT);
	paint_ellipse (img, 140.0f - 70.0f, 190.0f - 128.0f, 30.0f, 12.0f, &COL_SHIRT_HI);
	paint_ellipse (img, 140.0f + 70.0f, 190.0f - 128.0f, 30.0f, 12.0f, &COL_SHIRT_HI);

	/* V-neck: outline then skin */
	paint (img, 0, 0, BODY_W, BODY_H, inside_vneck, &plus, &COL_SHIRT_DK);
	paint (img, 0, 0, BODY_W, BODY_H, inside_vneck, &zero, &COL_SKIN);
	return img;
}

/*
 * The upper lid's edge in its atlas cell: a shallow parabola from 78 %
 * of the cell's height at the corners down to 2 px above its bottom in
 * the middle, so a closed lid reads as a gentle curved lash line and a
 * half-closed one as a heavy lid.  The curve stays below the white's
 * outline everywhere (the white is an oval inside the cell), so a lid
 * pulled all the way down hides the eye.
 */
#define LID_ARC_CORNER  (0.78f)

static gfloat
upper_lid_edge (gfloat  x,
                gfloat *slope)
{
	gfloat hw = (gfloat)EYE_CELL_W / 2.0f;
	gfloat a = CLAMP ((x - ((gfloat)ATLAS_UPPER_X + hw)) / hw, -1.0f, 1.0f);
	gfloat h = (gfloat)EYE_CELL_H - 2.0f;

	if (slope != NULL)
		*slope = -2.0f * h * (1.0f - LID_ARC_CORNER) * a / hw;
	return h * (LID_ARC_CORNER + (1.0f - LID_ARC_CORNER) * (1.0f - a * a));
}

/* Upper lid cell: skin above the edge. */
static gboolean
inside_upper_lid (gfloat        x,
                  gfloat        y,
                  gconstpointer data)
{
	if (x < (gfloat)ATLAS_UPPER_X || x > (gfloat)(ATLAS_UPPER_X + EYE_CELL_W))
		return FALSE;
	return y <= upper_lid_edge (x, NULL);
}

/* The lash: within 2.6 px of the edge (distance to the curve, estimated
 * from the vertical gap and the slope). */
static gboolean
inside_upper_lash (gfloat        x,
                   gfloat        y,
                   gconstpointer data)
{
	gfloat slope;
	gfloat yc;

	if (x < (gfloat)ATLAS_UPPER_X || x > (gfloat)(ATLAS_UPPER_X + EYE_CELL_W))
		return FALSE;
	yc = upper_lid_edge (x, &slope);
	return fabsf (y - yc) / sqrtf (1.0f + slope * slope) <= 2.6f;
}

/* Lower lid cell: skin below the upper half of the cell's oval. */
static gboolean
inside_lower_lid (gfloat        x,
                  gfloat        y,
                  gconstpointer data)
{
	gfloat cx = (gfloat)ATLAS_LOWER_X + (gfloat)EYE_CELL_W / 2.0f;
	gfloat a = (x - cx) / ((gfloat)EYE_CELL_W / 2.0f);
	gfloat half = (gfloat)EYE_CELL_H / 2.0f;

	if (x < (gfloat)ATLAS_LOWER_X || x > (gfloat)(ATLAS_LOWER_X + EYE_CELL_W))
		return FALSE;
	return y >= half - half * sqrtf (MAX (1.0f - a * a, 0.0f));
}

/* The eyes atlas, 228 x 72: white | iris | upper lid | lower lid. */
static GrlImage *
paint_eyes (void)
{
	GrlImage *img = new_clear_image (ATLAS_W, ATLAS_H);
	gfloat    wx = (gfloat)ATLAS_WHITE_X + (gfloat)EYE_CELL_W / 2.0f;
	gfloat    ix = (gfloat)ATLAS_IRIS_X + (gfloat)IRIS_CELL / 2.0f;
	gfloat    iy = (gfloat)IRIS_CELL / 2.0f;
	gfloat    my = (gfloat)EYE_CELL_H / 2.0f;
	Ring      lash;

	/* the white with an outline */
	paint_ellipse (img, wx, my, EYE_RX + 2.5f, EYE_RY + 2.5f, &COL_LINE);
	paint_ellipse (img, wx, my, EYE_RX, EYE_RY, &COL_WHITE);

	/* iris: dark rim, colour, pupil, two catch-lights */
	paint_ellipse (img, ix, iy, 15.5f, 15.5f, &COL_IRIS_DK);
	paint_ellipse (img, ix, iy, 13.0f, 13.0f, &COL_IRIS);
	paint_ellipse (img, ix, iy + 1.0f, 7.0f, 7.0f, &COL_PUPIL);
	paint_ellipse (img, ix - 5.0f, iy - 5.0f, 4.0f, 4.0f, &COL_WHITE);
	paint_ellipse (img, ix + 5.0f, iy + 5.5f, 1.8f, 1.8f, &COL_WHITE);

	/* upper lid: skin, and the lash line along its lower edge */
	paint (img, ATLAS_UPPER_X, 0, ATLAS_UPPER_X + EYE_CELL_W, EYE_CELL_H, inside_upper_lid, NULL, &COL_SKIN);
	paint (img, ATLAS_UPPER_X, 0, ATLAS_UPPER_X + EYE_CELL_W, EYE_CELL_H, inside_upper_lash, NULL, &COL_LASH);

	/* lower lid: skin with a soft crease along its upper edge */
	paint (img, ATLAS_LOWER_X, 0, ATLAS_LOWER_X + EYE_CELL_W, EYE_CELL_H, inside_lower_lid, NULL, &COL_SKIN);
	lash.e.cx = (gfloat)ATLAS_LOWER_X + (gfloat)EYE_CELL_W / 2.0f;
	lash.e.cy = my;
	lash.e.rx = (gfloat)EYE_CELL_W / 2.0f - 2.0f;
	lash.e.ry = my - 2.0f;
	lash.e.half = -1;
	lash.t = 1.6f;
	paint (img, ATLAS_LOWER_X, 0, ATLAS_LOWER_X + EYE_CELL_W, EYE_CELL_H, inside_ring, &lash, &COL_SKIN_DK);
	return img;
}

/* one brow, 64 x 20: a gently arched, tapering stroke */
static GrlImage *
paint_brow (void)
{
	GrlImage *img = new_clear_image (BROW_W, BROW_H);
	gfloat    pts[14];
	Stroke    s;
	guint     i;

	for (i = 0; i < 7; i++)
	{
		gfloat t = (gfloat)i / 6.0f * 2.0f - 1.0f;

		pts[i * 2] = 32.0f + t * 25.0f;
		pts[i * 2 + 1] = 12.5f - 5.0f * (1.0f - t * t);
	}
	s.pts = pts;
	s.n = 7;
	s.r_mid = 5.5f;
	s.r_end = 3.0f;
	paint (img, 0, 0, BROW_W, BROW_H, inside_stroke, &s, &COL_BROW);
	return img;
}

/* The mouth bands, 64 x 64: teeth (v 0 .. 0.3), interior (0.3 .. 0.7),
 * tongue (0.7 .. 1).  The interior darkens towards its middle. */
static GrlImage *
paint_mouth (void)
{
	GrlImage *img = new_clear_image (MOUTH_TEX, MOUTH_TEX);
	guint8   *px = grl_image_get_pixels (img, NULL);
	gint      x;
	gint      y;

	if (px == NULL)
		return img;
	for (y = 0; y < MOUTH_TEX; y++)
	{
		gfloat          v = ((gfloat)y + 0.5f) / (gfloat)MOUTH_TEX;
		const GrlColor *c;
		GrlColor        mix;

		if (v < MOUTH_TEETH_V)
		{
			/* teeth, with a faint shadow at their bottom edge */
			mix = COL_TEETH;
			if (v > MOUTH_TEETH_V - 0.05f)
			{
				mix.r = 218;
				mix.g = 214;
				mix.b = 210;
			}
			c = &mix;
		}
		else if (v < MOUTH_TONGUE_V)
		{
			gfloat m = 1.0f - fabsf ((v - 0.5f) / 0.2f);

			mix.r = (guint8)((gfloat)COL_MOUTH.r + ((gfloat)COL_MOUTH_DK.r - (gfloat)COL_MOUTH.r) * m);
			mix.g = (guint8)((gfloat)COL_MOUTH.g + ((gfloat)COL_MOUTH_DK.g - (gfloat)COL_MOUTH.g) * m);
			mix.b = (guint8)((gfloat)COL_MOUTH.b + ((gfloat)COL_MOUTH_DK.b - (gfloat)COL_MOUTH.b) * m);
			mix.a = 255;
			c = &mix;
		}
		else
			c = &COL_TONGUE;
		for (x = 0; x < MOUTH_TEX; x++)
		{
			guint8 *d = px + ((gsize)y * MOUTH_TEX + (gsize)x) * 4;

			d[0] = c->r;
			d[1] = c->g;
			d[2] = c->b;
			d[3] = 255;
		}
	}
	return img;
}

/* ------------------------------------------------------------------ */
/* Meshes                                                              */
/* ------------------------------------------------------------------ */

/* A grid part of a mesh: vertex (r, c) is first + r * (cols + 1) + c. */
typedef struct
{
	guint first;
	guint cols;
	guint rows;
} Part;

/* Appends a (cols x rows)-cell grid spanning (x0, y0) .. (x1, y1) in
 * setup space and (u0, v0) .. (u1, v1) of the texture; a zero-height
 * grid is fine (a collapsed lid). */
static Part
add_grid (LrgSkinnedSprite *s,
          gfloat            x0,
          gfloat            y0,
          gfloat            x1,
          gfloat            y1,
          guint             cols,
          guint             rows,
          gfloat            u0,
          gfloat            v0,
          gfloat            u1,
          gfloat            v1)
{
	Part  p;
	guint r;
	guint c;

	p.first = lrg_skinned_sprite_get_vertex_count (s);
	p.cols = cols;
	p.rows = rows;
	for (r = 0; r <= rows; r++)
	{
		for (c = 0; c <= cols; c++)
		{
			gfloat fu = (gfloat)c / (gfloat)cols;
			gfloat fv = (gfloat)r / (gfloat)rows;

			lrg_skinned_sprite_add_vertex (s, x0 + (x1 - x0) * fu, y0 + (y1 - y0) * fv,
			                               u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv);
		}
	}
	for (r = 0; r < rows; r++)
	{
		for (c = 0; c < cols; c++)
		{
			guint tl = p.first + r * (cols + 1) + c;
			guint tr = tl + 1;
			guint bl = tl + cols + 1;
			guint br = bl + 1;

			lrg_skinned_sprite_add_triangle (s, tl, tr, bl);
			lrg_skinned_sprite_add_triangle (s, tr, br, bl);
		}
	}
	return p;
}

/* A new sprite with @img as its CPU source image. */
static LrgSkinnedSprite *
new_sprite (GrlImage *img)
{
	LrgSkinnedSprite *s = lrg_skinned_sprite_new ();

	lrg_skinned_sprite_set_source_image (s, img);
	return s;
}

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

/* Squash and stretch: every vertex scaled about the feet (the origin),
 * on every slot, so the character keeps together. */
static void
add_squash_stretch (LrgSkinnedSprite *s)
{
	guint              nv = lrg_skinned_sprite_get_vertex_count (s);
	g_autofree gfloat *squash = g_new0 (gfloat, nv * 2);
	g_autofree gfloat *stretch = g_new0 (gfloat, nv * 2);
	guint              i;

	for (i = 0; i < nv; i++)
	{
		gfloat x;
		gfloat y;

		lrg_skinned_sprite_get_vertex (s, i, &x, &y, NULL, NULL);
		squash[i * 2] = x * 0.14f;
		squash[i * 2 + 1] = y * -0.12f;
		stretch[i * 2] = x * -0.10f;
		stretch[i * 2 + 1] = y * 0.12f;
	}
	lrg_skinned_sprite_add_shape (s, LRG_SHAPE_SQUASH, squash, nv * 2);
	lrg_skinned_sprite_add_shape (s, LRG_SHAPE_STRETCH, stretch, nv * 2);
}

/* Fraction from the outer (0) to the inner (1) end of a feature centred
 * at @cx with half width @half: the inner end faces the face's middle. */
static gfloat
inner_t (gfloat x,
         gfloat cx,
         gfloat half)
{
	gfloat a = CLAMP ((x - cx) / half, -1.0f, 1.0f);

	return cx < 0.0f ? (a + 1.0f) / 2.0f : (1.0f - a) / 2.0f;
}

/* ---- eyes ---- */

typedef struct
{
	Part white[2];   /* [0] the character's right eye (x < 0), [1] its left */
	Part iris[2];
	Part upper[2];
	Part lower[2];
} EyesRig;

/* What one eye shape does, at weight 1. */
typedef struct
{
	const gchar *name;
	gfloat       mask[2];      /* which eyes it moves ([0] right, [1] left) */
	gfloat       white_scale;  /* white grows by this fraction about its centre */
	gfloat       iris_dx;
	gfloat       iris_dy;
	gfloat       iris_scale;
	gfloat       lid_outer;    /* upper lid edge travel (down +) at the outer end */
	gfloat       lid_inner;    /* ... and at the inner end */
	gfloat       lid_top;      /* upper lid top edge travel */
	gfloat       lower_lift;   /* lower lid edge travel (up -) */
} EyePose;

static const EyePose eye_poses[] = {
	/* name                  mask       white  idx    idy   iscale  outer  inner   top   lower */
	{ LRG_SHAPE_BLINK_L,    { 0, 1 }, 0.00f,  0.0f,  0.0f,  0.00f, LID_CLOSE, LID_CLOSE, 0.0f, 0.0f },
	{ LRG_SHAPE_BLINK_R,    { 1, 0 }, 0.00f,  0.0f,  0.0f,  0.00f, LID_CLOSE, LID_CLOSE, 0.0f, 0.0f },
	{ LRG_SHAPE_SQUINT,     { 1, 1 }, 0.00f,  0.0f,  0.0f,  0.00f, 24.0f, 24.0f,  0.0f, -18.0f },
	{ LRG_SHAPE_WIDE,       { 1, 1 }, 0.10f,  0.0f,  0.0f, -0.12f, -4.0f, -4.0f, -4.0f, 0.0f },
	{ LRG_SHAPE_LOOK_LEFT,  { 1, 1 }, 0.00f, 10.0f,  0.0f,  0.00f,  0.0f,  0.0f,  0.0f, 0.0f },
	{ LRG_SHAPE_LOOK_RIGHT, { 1, 1 }, 0.00f, -10.0f, 0.0f,  0.00f,  0.0f,  0.0f,  0.0f, 0.0f },
	{ LRG_SHAPE_LOOK_UP,    { 1, 1 }, 0.00f,  0.0f, -9.0f,  0.00f,  0.0f,  0.0f,  0.0f, 0.0f },
	{ LRG_SHAPE_LOOK_DOWN,  { 1, 1 }, 0.00f,  0.0f,  9.0f,  0.00f, 10.0f, 10.0f,  0.0f, 0.0f },
	{ LRG_SHAPE_HAPPY,      { 1, 1 }, 0.00f,  0.0f,  0.0f,  0.00f,  6.0f,  6.0f,  0.0f, -24.0f },
	{ LRG_SHAPE_SAD,        { 1, 1 }, 0.00f,  0.0f,  3.0f,  0.00f, 16.0f,  4.0f,  0.0f, 0.0f },
	{ LRG_SHAPE_ANGRY,      { 1, 1 }, 0.00f,  0.0f,  0.0f, -0.05f,  4.0f, 16.0f,  0.0f, -8.0f },
	{ LRG_SHAPE_SURPRISED,  { 1, 1 }, 0.10f,  0.0f,  0.0f, -0.15f, -4.0f, -4.0f, -4.0f, 0.0f },
	{ LRG_SHAPE_SHOUT,      { 1, 1 }, 0.00f,  0.0f,  0.0f,  0.00f,  4.0f, 14.0f,  0.0f, -12.0f },
};

/* Fills @off with the offsets of @pose for every vertex of the eyes. */
static void
eye_pose_offsets (LrgSkinnedSprite *s,
                  const EyesRig    *rig,
                  const EyePose    *pose,
                  gfloat           *off)
{
	guint side;

	for (side = 0; side < 2; side++)
	{
		gfloat cx = side == 0 ? -EYE_X : EYE_X;
		gfloat m = pose->mask[side];
		guint  i;
		guint  n;

		if (m == 0.0f)
			continue;

		/* the white scales about the eye centre */
		n = (rig->white[side].cols + 1) * (rig->white[side].rows + 1);
		for (i = 0; i < n; i++)
		{
			guint  v = rig->white[side].first + i;
			gfloat x;
			gfloat y;

			lrg_skinned_sprite_get_vertex (s, v, &x, &y, NULL, NULL);
			off[v * 2] = (x - cx) * pose->white_scale;
			off[v * 2 + 1] = (y - EYE_Y) * pose->white_scale;
		}

		/* the iris moves and scales about its own centre */
		n = (rig->iris[side].cols + 1) * (rig->iris[side].rows + 1);
		for (i = 0; i < n; i++)
		{
			guint  v = rig->iris[side].first + i;
			gfloat x;
			gfloat y;

			lrg_skinned_sprite_get_vertex (s, v, &x, &y, NULL, NULL);
			off[v * 2] = pose->iris_dx + (x - cx) * pose->iris_scale;
			off[v * 2 + 1] = pose->iris_dy + (y - (EYE_Y + IRIS_DROP)) * pose->iris_scale;
		}

		/* upper lid: the top row by lid_top, the bottom row from
		 * lid_outer at the outer corner to lid_inner at the inner one;
		 * it widens with the white */
		n = (rig->upper[side].cols + 1) * (rig->upper[side].rows + 1);
		for (i = 0; i < n; i++)
		{
			guint  v = rig->upper[side].first + i;
			gboolean bottom = i > rig->upper[side].cols;
			gfloat x;
			gfloat y;

			lrg_skinned_sprite_get_vertex (s, v, &x, &y, NULL, NULL);
			off[v * 2] = (x - cx) * pose->white_scale;
			if (bottom)
				off[v * 2 + 1] = pose->lid_outer + (pose->lid_inner - pose->lid_outer) * inner_t (x, cx, (gfloat)EYE_CELL_W / 2.0f);
			else
				off[v * 2 + 1] = pose->lid_top;
		}

		/* lower lid: the top row lifts */
		n = (rig->lower[side].cols + 1) * (rig->lower[side].rows + 1);
		for (i = 0; i < n; i++)
		{
			guint  v = rig->lower[side].first + i;
			gfloat x;
			gfloat y;

			lrg_skinned_sprite_get_vertex (s, v, &x, &y, NULL, NULL);
			off[v * 2] = (x - cx) * pose->white_scale;
			if (i <= rig->lower[side].cols)
				off[v * 2 + 1] = pose->lower_lift;
		}
	}
}

static LrgSkinnedSprite *
build_eyes (void)
{
	g_autoptr(GrlImage) atlas = paint_eyes ();
	LrgSkinnedSprite   *s = new_sprite (atlas);
	EyesRig             rig;
	gfloat              hw = (gfloat)EYE_CELL_W / 2.0f;
	gfloat              hh = (gfloat)EYE_CELL_H / 2.0f;
	gfloat              hi = (gfloat)IRIS_CELL / 2.0f;
	guint               side;
	guint               i;

	/* the parts in drawing order: whites, irises, lower lids, upper lids
	 * (a closing upper lid covers a raised lower one); UVs address the
	 * atlas cells, which are 4 px apart so bilinear sampling never mixes
	 * two cells */
	for (side = 0; side < 2; side++)
	{
		gfloat cx = side == 0 ? -EYE_X : EYE_X;

		rig.white[side] = add_grid (s, cx - hw, EYE_Y - hh, cx + hw, EYE_Y + hh, 1, 1,
		                            (gfloat)ATLAS_WHITE_X / ATLAS_W, 0.0f,
		                            (gfloat)(ATLAS_WHITE_X + EYE_CELL_W) / ATLAS_W, 1.0f);
	}
	for (side = 0; side < 2; side++)
	{
		gfloat cx = side == 0 ? -EYE_X : EYE_X;
		gfloat cy = EYE_Y + IRIS_DROP;

		rig.iris[side] = add_grid (s, cx - hi, cy - hi, cx + hi, cy + hi, 1, 1,
		                           (gfloat)ATLAS_IRIS_X / ATLAS_W, 0.0f,
		                           (gfloat)(ATLAS_IRIS_X + IRIS_CELL) / ATLAS_W, (gfloat)IRIS_CELL / ATLAS_H);
	}
	for (side = 0; side < 2; side++)
	{
		gfloat cx = side == 0 ? -EYE_X : EYE_X;

		/* at rest the lower lid has no height at all */
		rig.lower[side] = add_grid (s, cx - hw, EYE_Y + hh, cx + hw, EYE_Y + hh, 4, 1,
		                            (gfloat)ATLAS_LOWER_X / ATLAS_W, 0.0f,
		                            (gfloat)(ATLAS_LOWER_X + EYE_CELL_W) / ATLAS_W, 1.0f);
	}

	for (side = 0; side < 2; side++)
	{
		gfloat cx = side == 0 ? -EYE_X : EYE_X;

		/* at rest the whole lid texture is folded into LID_REST (no
		 * height: the lid is hidden above the eye until a shape pulls
		 * its edge down) */
		rig.upper[side] = add_grid (s, cx - hw, EYE_Y - hh, cx + hw, EYE_Y - hh + LID_REST, 4, 1,
		                            (gfloat)ATLAS_UPPER_X / ATLAS_W, 0.0f,
		                            (gfloat)(ATLAS_UPPER_X + EYE_CELL_W) / ATLAS_W, 1.0f);
	}
	/* the shapes */
	for (i = 0; i < G_N_ELEMENTS (eye_poses); i++)
	{
		guint              nv = lrg_skinned_sprite_get_vertex_count (s);
		g_autofree gfloat *off = g_new0 (gfloat, nv * 2);

		eye_pose_offsets (s, &rig, &eye_poses[i], off);
		lrg_skinned_sprite_add_shape (s, eye_poses[i].name, off, nv * 2);
	}
	add_squash_stretch (s);
	return s;
}

/* ---- brows ---- */

/* What one brow shape does: vertical travel at the outer and inner
 * ends (up -), the middle arching by @arch. */
typedef struct
{
	const gchar *name;
	gfloat       outer;
	gfloat       inner;
	gfloat       arch;
} BrowPose;

static const BrowPose brow_poses[] = {
	{ LRG_SHAPE_BROW_UP,      -12.0f, -12.0f, -2.0f },
	{ LRG_SHAPE_BROW_DOWN,      8.0f,   8.0f,  0.0f },
	{ LRG_SHAPE_BROW_ANGRY,    -4.0f,  12.0f,  0.0f },
	{ LRG_SHAPE_BROW_WORRIED,   5.0f, -12.0f,  0.0f },
	{ LRG_SHAPE_HAPPY,         -6.0f,  -6.0f, -3.0f },
	{ LRG_SHAPE_SAD,            4.0f, -10.0f,  0.0f },
	{ LRG_SHAPE_ANGRY,         -3.0f,  13.0f,  0.0f },
	{ LRG_SHAPE_SURPRISED,    -16.0f, -16.0f, -4.0f },
	{ LRG_SHAPE_SHOUT,         -6.0f,  14.0f,  0.0f },
};

static LrgSkinnedSprite *
build_brows (void)
{
	g_autoptr(GrlImage) img = paint_brow ();
	LrgSkinnedSprite   *s = new_sprite (img);
	Part                part[2];
	gfloat              hw = (gfloat)BROW_W / 2.0f;
	gfloat              hh = (gfloat)BROW_H / 2.0f;
	guint               side;
	guint               i;

	/* the left brow (screen right) samples the texture mirrored, so both
	 * taper the same way */
	part[0] = add_grid (s, -BROW_X - hw, BROW_Y - hh, -BROW_X + hw, BROW_Y + hh, 3, 1, 0.0f, 0.0f, 1.0f, 1.0f);
	part[1] = add_grid (s, BROW_X - hw, BROW_Y - hh, BROW_X + hw, BROW_Y + hh, 3, 1, 1.0f, 0.0f, 0.0f, 1.0f);

	for (i = 0; i < G_N_ELEMENTS (brow_poses); i++)
	{
		guint              nv = lrg_skinned_sprite_get_vertex_count (s);
		g_autofree gfloat *off = g_new0 (gfloat, nv * 2);

		for (side = 0; side < 2; side++)
		{
			gfloat cx = side == 0 ? -BROW_X : BROW_X;
			guint  n = (part[side].cols + 1) * (part[side].rows + 1);
			guint  k;

			for (k = 0; k < n; k++)
			{
				guint  v = part[side].first + k;
				gfloat x;
				gfloat y;
				gfloat t;
				gfloat a;

				lrg_skinned_sprite_get_vertex (s, v, &x, &y, NULL, NULL);
				t = inner_t (x, cx, hw);
				a = (x - cx) / hw;
				off[v * 2 + 1] = brow_poses[i].outer + (brow_poses[i].inner - brow_poses[i].outer) * t
				                 + brow_poses[i].arch * (1.0f - a * a);
			}
		}
		lrg_skinned_sprite_add_shape (s, brow_poses[i].name, off, nv * 2);
	}
	add_squash_stretch (s);
	return s;
}

/* ---- mouth ---- */

/*
 * A mouth pose: the opening's width scale, how far the upper and lower
 * edges open at the centre, the profile exponent (1 a flat lens, 0.5
 * round), how much of the opening the teeth (from the top) and the
 * tongue (from the bottom) fill, the corners' extra drop (down +) and a
 * vertical shift.
 */
typedef struct
{
	const gchar *name;
	gfloat       width;
	gfloat       up;
	gfloat       down;
	gfloat       profile;
	gfloat       teeth;
	gfloat       tongue;
	gfloat       corners;
	gfloat       shift;
} MouthPose;

static const MouthPose mouth_rest = { LRG_SHAPE_REST, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };

static const MouthPose mouth_poses[] = {
	/* name                    width    up   down  prof  teeth tongue corners shift */
	{ LRG_SHAPE_REST,          1.00f,  0.0f,  0.0f, 1.0f, 0.00f, 0.00f,  0.0f,  0.0f },
	{ LRG_SHAPE_A,             1.04f, 10.0f, 28.0f, 0.7f, 0.18f, 0.22f,  2.0f,  0.0f },
	{ LRG_SHAPE_E,             1.16f,  7.0f, 13.0f, 1.0f, 0.30f, 0.14f,  0.0f,  0.0f },
	{ LRG_SHAPE_I,             1.22f,  4.0f,  7.0f, 1.2f, 0.50f, 0.00f, -2.0f,  0.0f },
	{ LRG_SHAPE_O,             0.62f, 13.0f, 22.0f, 0.5f, 0.08f, 0.16f,  0.0f,  0.0f },
	{ LRG_SHAPE_U,             0.46f,  6.0f, 10.0f, 0.5f, 0.00f, 0.12f,  0.0f,  1.0f },
	{ LRG_SHAPE_MBP,           0.94f, -0.5f, -0.5f, 1.0f, 0.00f, 0.00f,  4.0f,  0.0f },
	{ LRG_SHAPE_FV,            0.96f,  4.0f,  4.0f, 1.0f, 0.70f, 0.00f,  0.0f,  0.0f },
	{ LRG_SHAPE_L,             0.96f,  8.0f, 17.0f, 0.8f, 0.14f, 0.52f,  0.0f,  0.0f },
	{ LRG_SHAPE_WQ,            0.40f,  4.0f,  6.0f, 0.5f, 0.00f, 0.00f,  0.0f,  1.0f },
	{ LRG_SHAPE_CDGKNRSTYZ,    1.06f,  4.0f,  6.0f, 1.0f, 0.52f, 0.06f,  0.0f,  0.0f },
	{ LRG_SHAPE_HAPPY,         1.08f,  0.0f,  6.0f, 1.0f, 0.28f, 0.00f, -12.0f, 0.0f },
	{ LRG_SHAPE_SAD,           0.86f,  0.0f,  0.0f, 1.0f, 0.00f, 0.00f, 13.0f, -2.0f },
	{ LRG_SHAPE_ANGRY,         0.86f,  3.0f,  4.0f, 1.0f, 0.55f, 0.00f,  7.0f,  0.0f },
	{ LRG_SHAPE_SURPRISED,     0.60f, 12.0f, 18.0f, 0.5f, 0.05f, 0.10f,  0.0f,  0.0f },
	{ LRG_SHAPE_SHOUT,         1.12f, 16.0f, 36.0f, 0.6f, 0.15f, 0.20f,  5.0f,  0.0f },
};

/* Where vertex (@row, @col) of the mouth sits in @pose. */
static void
mouth_vertex (const MouthPose *pose,
              guint            row,
              guint            col,
              gfloat          *x,
              gfloat          *y)
{
	gfloat t = ((gfloat)col / (gfloat)(MOUTH_COLS - 1)) * 2.0f - 1.0f;
	gfloat t2 = t * t;
	gfloat mid = MOUTH_Y + pose->shift - MOUTH_SMILE * t2 + pose->corners * t2;
	gfloat prof = powf (MAX (1.0f - t2, 0.0f), pose->profile);
	gfloat top = mid - (MOUTH_REST_H + pose->up) * prof;
	gfloat bottom = mid + (MOUTH_REST_H + pose->down) * prof;
	gfloat open = bottom - top;

	*x = MOUTH_HALF_W * pose->width * t;
	switch (row)
	{
	case 0:
		*y = top;
		break;
	case 1:
		*y = top + pose->teeth * open;
		break;
	case 2:
		*y = bottom - pose->tongue * open;
		break;
	default:
		*y = bottom;
		break;
	}
}

static LrgSkinnedSprite *
build_mouth (void)
{
	static const gfloat band_v[MOUTH_ROWS] = { 0.0f, MOUTH_TEETH_V, MOUTH_TONGUE_V, 1.0f };
	g_autoptr(GrlImage) img = paint_mouth ();
	LrgSkinnedSprite   *s = new_sprite (img);
	guint               r;
	guint               c;
	guint               i;

	/* vertices row by row at the rest pose; each row samples its band
	 * boundary, so the bands stretch with the rows */
	for (r = 0; r < MOUTH_ROWS; r++)
	{
		for (c = 0; c < MOUTH_COLS; c++)
		{
			gfloat x;
			gfloat y;

			mouth_vertex (&mouth_rest, r, c, &x, &y);
			lrg_skinned_sprite_add_vertex (s, x, y, (gfloat)c / (gfloat)(MOUTH_COLS - 1), band_v[r]);
		}
	}
	for (r = 0; r + 1 < MOUTH_ROWS; r++)
	{
		for (c = 0; c + 1 < MOUTH_COLS; c++)
		{
			guint tl = r * MOUTH_COLS + c;

			lrg_skinned_sprite_add_triangle (s, tl, tl + 1, tl + MOUTH_COLS);
			lrg_skinned_sprite_add_triangle (s, tl + 1, tl + MOUTH_COLS + 1, tl + MOUTH_COLS);
		}
	}

	/* every pose as offsets from the rest pose */
	for (i = 0; i < G_N_ELEMENTS (mouth_poses); i++)
	{
		gfloat off[MOUTH_ROWS * MOUTH_COLS * 2];

		for (r = 0; r < MOUTH_ROWS; r++)
		{
			for (c = 0; c < MOUTH_COLS; c++)
			{
				guint  v = r * MOUTH_COLS + c;
				gfloat x0;
				gfloat y0;
				gfloat x1;
				gfloat y1;

				mouth_vertex (&mouth_rest, r, c, &x0, &y0);
				mouth_vertex (&mouth_poses[i], r, c, &x1, &y1);
				off[v * 2] = x1 - x0;
				off[v * 2 + 1] = y1 - y0;
			}
		}
		lrg_skinned_sprite_add_shape (s, mouth_poses[i].name, off, G_N_ELEMENTS (off));
	}
	add_squash_stretch (s);
	return s;
}

/* ---- head and body ---- */

static LrgSkinnedSprite *
build_head (void)
{
	g_autoptr(GrlImage) img = paint_head ();
	LrgSkinnedSprite   *s = new_sprite (img);

	add_grid (s, HEAD_X0, HEAD_Y0, HEAD_X0 + HEAD_W, HEAD_Y0 + HEAD_H, 4, 4, 0.0f, 0.0f, 1.0f, 1.0f);
	add_squash_stretch (s);
	return s;
}

static LrgSkinnedSprite *
build_body (void)
{
	g_autoptr(GrlImage) img = paint_body ();
	LrgSkinnedSprite   *s = new_sprite (img);
	guint               nv;
	g_autofree gfloat  *breathe = NULL;
	guint               i;

	add_grid (s, BODY_X0, BODY_Y0, BODY_X0 + BODY_W, BODY_Y0 + BODY_H, 4, 4, 0.0f, 0.0f, 1.0f, 1.0f);

	/* breathe: the chest widens and the shoulders rise, more towards the
	 * top; the hem stays put */
	nv = lrg_skinned_sprite_get_vertex_count (s);
	breathe = g_new0 (gfloat, nv * 2);
	for (i = 0; i < nv; i++)
	{
		gfloat x;
		gfloat y;
		gfloat f;

		lrg_skinned_sprite_get_vertex (s, i, &x, &y, NULL, NULL);
		f = CLAMP (-y / 165.0f, 0.0f, 1.0f);
		breathe[i * 2] = x * 0.045f * f;
		breathe[i * 2 + 1] = -7.0f * f;
	}
	lrg_skinned_sprite_add_shape (s, LRG_SHAPE_BREATHE, breathe, nv * 2);
	add_squash_stretch (s);
	return s;
}

/* ------------------------------------------------------------------ */
/* Public                                                              */
/* ------------------------------------------------------------------ */

LrgSkeleton2D *
lrg_demo_character_new (void)
{
	g_autoptr(LrgSkeleton2D)    skel = lrg_skeleton2d_new ();
	g_autoptr(LrgSkinnedSprite) body = build_body ();
	g_autoptr(LrgSkinnedSprite) head = build_head ();
	g_autoptr(LrgSkinnedSprite) eyes = build_eyes ();
	g_autoptr(LrgSkinnedSprite) brows = build_brows ();
	g_autoptr(LrgSkinnedSprite) mouth = build_mouth ();
	gint                        root;
	gint                        bbody;
	gint                        bhead;
	gint                        slot;

	lrg_skeleton2d_set_name (skel, "demo-character");

	/* bones: the body at the feet, the head pivoting at the neck */
	root = lrg_skeleton2d_add_bone (skel, LRG_DEMO_BONE_ROOT, -1, 0, 0, 0, 1, 1, 0);
	bbody = lrg_skeleton2d_add_bone (skel, LRG_DEMO_BONE_BODY, root, 0, 0, 0, 1, 1, 0);
	bhead = lrg_skeleton2d_add_bone (skel, LRG_DEMO_BONE_HEAD, bbody, 0, NECK_Y, 0, 1, 1, 0);

	/* slots back to front, each showing its mesh (no vertex weights:
	 * the slot's bone carries the mesh rigidly) */
	slot = lrg_skeleton2d_add_slot (skel, LRG_DEMO_SLOT_BODY, bbody, "body");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "body", body);
	slot = lrg_skeleton2d_add_slot (skel, LRG_DEMO_SLOT_HEAD, bhead, "head");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "head", head);
	slot = lrg_skeleton2d_add_slot (skel, LRG_DEMO_SLOT_EYES, bhead, "eyes");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "eyes", eyes);
	slot = lrg_skeleton2d_add_slot (skel, LRG_DEMO_SLOT_BROWS, bhead, "brows");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "brows", brows);
	slot = lrg_skeleton2d_add_slot (skel, LRG_DEMO_SLOT_MOUTH, bhead, "mouth");
	lrg_skeleton2d_set_attachment (skel, NULL, slot, "mouth", mouth);

	lrg_skeleton2d_reset_pose (skel);
	lrg_skeleton2d_update_world (skel);
	return g_steal_pointer (&skel);
}

/* The attachment slot @slot shows, or NULL. */
static LrgSkinnedSprite *
slot_sprite (LrgSkeleton2D *skel,
             gint           slot)
{
	const gchar *name = lrg_skeleton2d_get_slot_attachment (skel, slot);

	return name != NULL ? lrg_skeleton2d_get_attachment (skel, slot, name) : NULL;
}

guint
lrg_demo_character_set_shape (LrgSkeleton2D *skel,
                              const gchar   *shape,
                              gfloat         weight)
{
	guint n = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (skel), 0);
	g_return_val_if_fail (shape != NULL, 0);

	for (i = 0; i < lrg_skeleton2d_get_slot_count (skel); i++)
	{
		LrgSkinnedSprite *s = slot_sprite (skel, (gint)i);

		if (s != NULL && lrg_skinned_sprite_find_shape (s, shape) >= 0)
		{
			lrg_skeleton2d_set_shape_weight (skel, (gint)i, shape, weight);
			n++;
		}
	}
	return n;
}

guint
lrg_demo_character_key_shape (LrgSkeleton2D     *skel,
                              LrgSkeleton2DClip *clip,
                              const gchar       *shape,
                              gfloat             time,
                              gfloat             weight)
{
	guint n = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_SKELETON2D (skel), 0);
	g_return_val_if_fail (clip != NULL, 0);
	g_return_val_if_fail (shape != NULL, 0);

	for (i = 0; i < lrg_skeleton2d_get_slot_count (skel); i++)
	{
		LrgSkinnedSprite *s = slot_sprite (skel, (gint)i);

		if (s != NULL && lrg_skinned_sprite_find_shape (s, shape) >= 0)
		{
			lrg_skeleton2d_clip_add_shape_key (clip, lrg_skeleton2d_get_slot_name (skel, (gint)i),
			                                   shape, time, weight, LRG_SKELETON2D_CURVE_LINEAR);
			n++;
		}
	}
	return n;
}

void
lrg_demo_character_get_region (LrgDemoRegion  region,
                               gfloat        *x,
                               gfloat        *y,
                               gfloat        *width,
                               gfloat        *height)
{
	gfloat bx;
	gfloat by;
	gfloat bw;
	gfloat bh;

	switch (region)
	{
	case LRG_DEMO_REGION_MOUTH:
		bx = -70.0f;
		by = MOUTH_Y - 30.0f;
		bw = 140.0f;
		bh = 72.0f;
		break;
	case LRG_DEMO_REGION_EYE_LEFT:
	case LRG_DEMO_REGION_EYE_RIGHT:
		bx = (region == LRG_DEMO_REGION_EYE_LEFT ? EYE_X : -EYE_X) - 32.0f;
		by = EYE_Y - 38.0f;
		bw = 64.0f;
		bh = 76.0f;
		break;
	case LRG_DEMO_REGION_FACE:
	default:
		bx = HEAD_X0;
		by = HEAD_Y0;
		bw = (gfloat)HEAD_W;
		bh = (gfloat)HEAD_H;
		break;
	}
	if (x != NULL)
		*x = bx;
	if (y != NULL)
		*y = by;
	if (width != NULL)
		*width = bw;
	if (height != NULL)
		*height = bh;
}
