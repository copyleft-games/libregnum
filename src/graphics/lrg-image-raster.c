/* lrg-image-raster.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * CPU textured-triangle rasteriser: fixed-point edge functions with the
 * top-left fill rule, affine UV interpolation, bilinear premultiplied
 * sampling, tint and source-over onto an RGBA8 GrlImage.
 */

#include "config.h"

#include <math.h>

#include "graphics/lrg-image-raster.h"

/* Corners are snapped to 1/SUBPIXEL of a pixel.  With 8 sub-pixel bits
 * and coordinates limited to 2^21 pixels a coordinate is below 2^29, an
 * edge vector below 2^30 and an edge function value below 2^61, so the
 * coverage arithmetic fits a gint64 exactly. */
#define SUBPIXEL_BITS  (8)
#define SUBPIXEL       ((gint64)1 << SUBPIXEL_BITS)
#define HALF_SUBPIXEL  (SUBPIXEL / 2)
#define COORD_LIMIT    (2097152.0f)

#define INV_255        (1.0f / 255.0f)

/* The sampled texture: its RGBA8 bytes and size. */
typedef struct
{
	const guint8 *px;
	gint          w;
	gint          h;
} RasterTexture;

/* floor (a / b) for b > 0 (C division truncates toward zero) */
static gint64
floor_div (gint64 a,
           gint64 b)
{
	gint64 q = a / b;

	if (a % b != 0 && a < 0)
		q--;
	return q;
}

/* The edge function of the directed edge a -> b at p: twice the signed
 * area of (a, b, p).  It is positive on the inside of an edge of a
 * triangle whose corners have a positive edge function, zero on the
 * edge's line. */
static gint64
edge_eval (gint64 ax,
           gint64 ay,
           gint64 bx,
           gint64 by,
           gint64 px,
           gint64 py)
{
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/*
 * The top-left rule for a triangle wound so its edge functions are
 * positive inside (clockwise on the y-down screen).  For that winding a
 * top edge runs to the right (dy == 0, dx > 0) and a left edge runs up
 * (dy < 0).  The rule picks exactly half of all edge directions: the
 * opposite direction (-dx, -dy) is never also accepted, so of two
 * triangles sharing an edge (which walk it in opposite directions) exactly
 * one owns the pixel centres lying on it.
 */
static gboolean
edge_is_top_left (gint64 ax,
                  gint64 ay,
                  gint64 bx,
                  gint64 by)
{
	gint64 dx = bx - ax;
	gint64 dy = by - ay;

	return dy < 0 || (dy == 0 && dx > 0);
}

/*
 * Bilinear sample of @t at (@u, @v) in premultiplied alpha, 0 .. 1 per
 * channel.  The coordinates are clamped to the image, texel centres sit
 * at (i + 0.5) / w, and neighbours outside the image repeat the border
 * texel (clamp to edge).  Each texel is premultiplied before it is
 * weighted, so a transparent texel adds nothing, not even its colour.
 */
static void
sample_bilinear (const RasterTexture *t,
                 gfloat               u,
                 gfloat               v,
                 gfloat              *out)
{
	const guint8 *p[4];
	gfloat        w[4];
	gfloat        fx;
	gfloat        fy;
	gfloat        ax;
	gfloat        ay;
	gint          x0;
	gint          y0;
	gint          x1;
	gint          y1;
	guint         k;

	/* texel space; the clamp also keeps NaN and huge values out of the
	 * float to int conversion */
	u = isnan (u) ? 0.0f : CLAMP (u, 0.0f, 1.0f);
	v = isnan (v) ? 0.0f : CLAMP (v, 0.0f, 1.0f);
	fx = u * (gfloat)t->w - 0.5f;
	fy = v * (gfloat)t->h - 0.5f;
	x0 = (gint)floorf (fx);
	y0 = (gint)floorf (fy);
	ax = fx - (gfloat)x0;
	ay = fy - (gfloat)y0;
	x1 = x0 + 1;
	y1 = y0 + 1;

	/* clamp to edge */
	x0 = CLAMP (x0, 0, t->w - 1);
	x1 = CLAMP (x1, 0, t->w - 1);
	y0 = CLAMP (y0, 0, t->h - 1);
	y1 = CLAMP (y1, 0, t->h - 1);

	p[0] = t->px + ((gsize)y0 * (gsize)t->w + (gsize)x0) * 4;
	p[1] = t->px + ((gsize)y0 * (gsize)t->w + (gsize)x1) * 4;
	p[2] = t->px + ((gsize)y1 * (gsize)t->w + (gsize)x0) * 4;
	p[3] = t->px + ((gsize)y1 * (gsize)t->w + (gsize)x1) * 4;
	w[0] = (1.0f - ax) * (1.0f - ay);
	w[1] = ax * (1.0f - ay);
	w[2] = (1.0f - ax) * ay;
	w[3] = ax * ay;

	/* weighted sum of premultiplied texels */
	out[0] = out[1] = out[2] = out[3] = 0.0f;
	for (k = 0; k < 4; k++)
	{
		gfloat a = (gfloat)p[k][3] * INV_255 * w[k];

		out[0] += (gfloat)p[k][0] * INV_255 * a;
		out[1] += (gfloat)p[k][1] * INV_255 * a;
		out[2] += (gfloat)p[k][2] * INV_255 * a;
		out[3] += a;
	}
}

/* 0 .. 1 to a rounded byte */
static guint8
to_byte (gfloat f)
{
	return (guint8)(CLAMP (f, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/*
 * Source-over of the premultiplied colour @s onto the straight-alpha
 * RGBA8 pixel @d:
 *   out_a   = s_a + d_a * (1 - s_a)
 *   out_rgb = (s_rgb + d_rgb * d_a * (1 - s_a)) / out_a
 * An (almost) transparent source leaves the pixel alone, an opaque one
 * replaces it.
 */
static void
blend_over (guint8       *d,
            const gfloat *s)
{
	gfloat sa = s[3];
	gfloat k;
	gfloat oa;

	if (sa < 0.5f * INV_255)
		return;
	if (sa > 1.0f - 0.5f * INV_255)
	{
		d[0] = to_byte (s[0] / sa);
		d[1] = to_byte (s[1] / sa);
		d[2] = to_byte (s[2] / sa);
		d[3] = 255;
		return;
	}
	k = (gfloat)d[3] * INV_255 * (1.0f - sa);
	oa = sa + k;
	d[0] = to_byte ((s[0] + (gfloat)d[0] * INV_255 * k) / oa);
	d[1] = to_byte ((s[1] + (gfloat)d[1] * INV_255 * k) / oa);
	d[2] = to_byte ((s[2] + (gfloat)d[2] * INV_255 * k) / oa);
	d[3] = to_byte (oa);
}

void
lrg_image_draw_triangle_textured (GrlImage       *dst,
                                  GrlImage       *src,
                                  const gfloat   *xy,
                                  const gfloat   *uv,
                                  const GrlColor *tint)
{
	RasterTexture tex = { NULL, 0, 0 };
	GrlRectangle  clip;
	guint8       *pixels;
	gint64        X[3];
	gint64        Y[3];
	gfloat        U[3] = { 0.0f, 0.0f, 0.0f };
	gfloat        V[3] = { 0.0f, 0.0f, 0.0f };
	gint          order[3] = { 0, 1, 2 };
	gint64        area;
	gint64        min_x;
	gint64        max_x;
	gint64        min_y;
	gint64        max_y;
	gint64        px0;
	gint64        px1;
	gint64        py0;
	gint64        py1;
	gint64        sx[3];
	gint64        sy[3];
	gint64        row[3];
	gint64        bias[3];
	gint64        cx;
	gint64        cy;
	gdouble       inv_area;
	gfloat        du1;
	gfloat        du2;
	gfloat        dv1;
	gfloat        dv2;
	gfloat        tr = 1.0f;
	gfloat        tg = 1.0f;
	gfloat        tb = 1.0f;
	gfloat        ta = 1.0f;
	gint          width;
	gint          height;
	gint          cx0;
	gint          cy0;
	gint          cx1;
	gint          cy1;
	gint64        x;
	gint64        y;
	gint          i;

	g_return_if_fail (GRL_IS_IMAGE (dst));
	g_return_if_fail (src == NULL || GRL_IS_IMAGE (src));
	g_return_if_fail (xy != NULL);
	g_return_if_fail (src == NULL || uv != NULL);

	/* --- corners: finite, in range, snapped to the sub-pixel grid --- */
	for (i = 0; i < 6; i++)
	{
		if (!isfinite (xy[i]) || fabsf (xy[i]) > COORD_LIMIT)
			return;
	}

	/* --- winding: make the edge functions positive inside; a triangle
	 * with no area (after snapping) draws nothing --- */
	{
		gint64 sxs[3];
		gint64 sys[3];

		for (i = 0; i < 3; i++)
		{
			sxs[i] = (gint64)floor ((gdouble)xy[i * 2] * (gdouble)SUBPIXEL + 0.5);
			sys[i] = (gint64)floor ((gdouble)xy[i * 2 + 1] * (gdouble)SUBPIXEL + 0.5);
		}
		area = edge_eval (sxs[0], sys[0], sxs[1], sys[1], sxs[2], sys[2]);
		if (area == 0)
			return;
		if (area < 0)
		{
			order[1] = 2;
			order[2] = 1;
			area = -area;
		}
		for (i = 0; i < 3; i++)
		{
			X[i] = sxs[order[i]];
			Y[i] = sys[order[i]];
			if (uv != NULL)
			{
				U[i] = uv[order[i] * 2];
				V[i] = uv[order[i] * 2 + 1];
			}
		}
	}

	/* --- destination and its clip rectangle --- */
	width = grl_image_get_width (dst);
	height = grl_image_get_height (dst);
	if (width <= 0 || height <= 0)
		return;
	pixels = grl_image_get_pixels (dst, NULL);
	if (pixels == NULL)
		return;
	cx0 = 0;
	cy0 = 0;
	cx1 = width;
	cy1 = height;
	if (grl_image_get_clip_rect (dst, &clip))
	{
		gint x0c = (gint)clip.x;
		gint y0c = (gint)clip.y;
		gint x1c = x0c + (gint)clip.width;
		gint y1c = y0c + (gint)clip.height;

		cx0 = MAX (cx0, x0c);
		cy0 = MAX (cy0, y0c);
		cx1 = MIN (cx1, x1c);
		cy1 = MIN (cy1, y1c);
	}
	if (cx0 >= cx1 || cy0 >= cy1)
		return;

	/* --- bounding box: the pixels whose centres lie inside the corners'
	 * bounds, clipped to the destination --- */
	min_x = MIN (X[0], MIN (X[1], X[2]));
	max_x = MAX (X[0], MAX (X[1], X[2]));
	min_y = MIN (Y[0], MIN (Y[1], Y[2]));
	max_y = MAX (Y[0], MAX (Y[1], Y[2]));
	px0 = -floor_div (-(min_x - HALF_SUBPIXEL), SUBPIXEL);   /* ceil */
	px1 = floor_div (max_x - HALF_SUBPIXEL, SUBPIXEL);
	py0 = -floor_div (-(min_y - HALF_SUBPIXEL), SUBPIXEL);
	py1 = floor_div (max_y - HALF_SUBPIXEL, SUBPIXEL);
	px0 = MAX (px0, (gint64)cx0);
	py0 = MAX (py0, (gint64)cy0);
	px1 = MIN (px1, (gint64)cx1 - 1);
	py1 = MIN (py1, (gint64)cy1 - 1);
	if (px0 > px1 || py0 > py1)
		return;

	/* --- texture and tint --- */
	if (src != NULL)
	{
		tex.w = grl_image_get_width (src);
		tex.h = grl_image_get_height (src);
		tex.px = tex.w > 0 && tex.h > 0 ? grl_image_get_pixels (src, NULL) : NULL;
		if (tex.px == NULL)
			return;
	}
	if (tint != NULL)
	{
		tr = (gfloat)tint->r * INV_255;
		tg = (gfloat)tint->g * INV_255;
		tb = (gfloat)tint->b * INV_255;
		ta = (gfloat)tint->a * INV_255;
		if (tint->a == 0)
			return;
	}

	/*
	 * --- edge functions, set up incrementally ---
	 * Edge i is the one opposite corner i, so its value is the
	 * (unnormalised) barycentric weight of corner i.  Stepping one pixel
	 * right adds sx[i], one pixel down sy[i].  bias[i] is -1 for an edge
	 * that is not top-left, which turns "on the edge" (0) into outside.
	 */
	for (i = 0; i < 3; i++)
	{
		gint a = (i + 1) % 3;
		gint b = (i + 2) % 3;

		sx[i] = -(Y[b] - Y[a]) * SUBPIXEL;
		sy[i] = (X[b] - X[a]) * SUBPIXEL;
		bias[i] = edge_is_top_left (X[a], Y[a], X[b], Y[b]) ? 0 : -1;
	}
	cx = px0 * SUBPIXEL + HALF_SUBPIXEL;
	cy = py0 * SUBPIXEL + HALF_SUBPIXEL;
	for (i = 0; i < 3; i++)
	{
		gint a = (i + 1) % 3;
		gint b = (i + 2) % 3;

		row[i] = edge_eval (X[a], Y[a], X[b], Y[b], cx, cy);
	}

	/* UV as a function of the barycentric weights of corners 1 and 2 */
	inv_area = 1.0 / (gdouble)area;
	du1 = U[1] - U[0];
	du2 = U[2] - U[0];
	dv1 = V[1] - V[0];
	dv2 = V[2] - V[0];

	/* --- scan the box; a row of a convex shape is one span, so a row
	 * stops at the first outside pixel after an inside one --- */
	for (y = py0; y <= py1; y++)
	{
		guint8   *line = pixels + (gsize)y * (gsize)width * 4;
		gint64    w0 = row[0];
		gint64    w1 = row[1];
		gint64    w2 = row[2];
		gboolean  entered = FALSE;

		for (x = px0; x <= px1; x++)
		{
			if (w0 + bias[0] >= 0 && w1 + bias[1] >= 0 && w2 + bias[2] >= 0)
			{
				gfloat s[4];

				entered = TRUE;
				if (src != NULL)
				{
					gfloat l1 = (gfloat)((gdouble)w1 * inv_area);
					gfloat l2 = (gfloat)((gdouble)w2 * inv_area);

					sample_bilinear (&tex,
					                 U[0] + l1 * du1 + l2 * du2,
					                 V[0] + l1 * dv1 + l2 * dv2,
					                 s);
				}
				else
				{
					s[0] = s[1] = s[2] = s[3] = 1.0f;
				}

				/* tint, premultiplied: colour by rgb * a, alpha by a */
				s[0] *= tr * ta;
				s[1] *= tg * ta;
				s[2] *= tb * ta;
				s[3] *= ta;
				blend_over (line + (gsize)x * 4, s);
			}
			else if (entered)
			{
				break;
			}
			w0 += sx[0];
			w1 += sx[1];
			w2 += sx[2];
		}
		row[0] += sy[0];
		row[1] += sy[1];
		row[2] += sy[2];
	}
}
