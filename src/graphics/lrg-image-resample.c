/* lrg-image-resample.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Gamma-correct, alpha-correct downsampling.
 *
 * The pipeline of lrg_image_downsample():
 *   1. decode   sRGB bytes -> linear floats (a 256-entry table), alpha / 255
 *   2. premul   rgb *= a, so a transparent pixel carries no colour weight
 *   3. filter   separable: every output column gathers taps from its
 *               source row, then every output row gathers from those
 *               columns; the weights of one output pixel sum to 1
 *   4. clamp    a into 0..1 and rgb into 0..a: the negative lobes of the
 *               Mitchell and Lanczos kernels over- and undershoot at hard
 *               edges, and a premultiplied colour above its alpha would
 *               unpremultiply to more than white
 *   5. unpremul rgb /= a, then the exact sRGB encode, rounded to bytes
 *
 * Kernels are written in output-pixel units t: a source pixel s
 * contributes kernel ((s + 0.5 - c) / factor) to the output pixel whose
 * centre is c = (o + 0.5) factor in source pixels.
 */

#include "config.h"

#define LRG_LOG_DOMAIN "Libregnum-Graphics"
#include "lrg-image-resample.h"
#include "../lrg-log.h"
#include <math.h>

/* One axis of the separable filter: for every output index the source
 * indices (already clamped to the edge) and the normalised weights,
 * stored as n_out rows of n_taps entries. */
typedef struct
{
	gint    n_taps;
	gint   *index;
	gfloat *weight;
} ResampleAxis;

/* ==========================================================================
 * Kernels
 * ========================================================================== */

/* filter_support: kernel radius in output pixels */
static gfloat
filter_support (LrgResampleFilter filter)
{
	switch (filter)
	{
	case LRG_RESAMPLE_FILTER_MITCHELL:
		return 2.0f;
	case LRG_RESAMPLE_FILTER_LANCZOS3:
		return 3.0f;
	case LRG_RESAMPLE_FILTER_BOX:
	default:
		return 0.5f;
	}
}

/* filter_kernel: the kernel's weight at distance t (output pixels) */
static gdouble
filter_kernel (LrgResampleFilter filter,
               gdouble           t)
{
	gdouble x;

	x = fabs (t);
	switch (filter)
	{
	case LRG_RESAMPLE_FILTER_MITCHELL:
		/* Mitchell-Netravali with B = C = 1/3, the general cubic
		 * (1/6) { (12-9B-6C)x^3 + (-18+12B+6C)x^2 + (6-2B)          x < 1
		 *         (-B-6C)x^3 + (6B+30C)x^2 + (-12B-48C)x + (8B+24C) x < 2 } */
		if (x < 1.0)
			return (7.0 * x * x * x - 12.0 * x * x + 16.0 / 3.0) / 6.0;
		if (x < 2.0)
			return ((-7.0 / 3.0) * x * x * x + 12.0 * x * x - 20.0 * x + 32.0 / 3.0) / 6.0;
		return 0.0;

	case LRG_RESAMPLE_FILTER_LANCZOS3:
		/* sinc (x) sinc (x / 3), sinc (x) = sin (pi x) / (pi x) */
		if (x < 1e-8)
			return 1.0;
		if (x < 3.0)
			return 3.0 * sin (G_PI * x) * sin (G_PI * x / 3.0) / (G_PI * G_PI * x * x);
		return 0.0;

	case LRG_RESAMPLE_FILTER_BOX:
	default:
		/* the factor source pixels of the block sit strictly inside
		 * |t| < 0.5; the neighbours start at |t| >= 0.5 + 0.5 / factor */
		return x < 0.5 ? 1.0 : 0.0;
	}
}

/* resample_axis_init: the taps of every output index along one axis */
static void
resample_axis_init (ResampleAxis      *axis,
                    gint               n_src,
                    gint               n_out,
                    guint              factor,
                    LrgResampleFilter  filter)
{
	gdouble f;
	gdouble radius;
	gint    o;

	f = (gdouble) factor;
	radius = (gdouble) filter_support (filter) * f;

	/* the widest window [c - radius, c + radius] spans 2 radius + 1
	 * source pixels; taps outside the kernel get weight 0 */
	axis->n_taps = (gint) ceil (2.0 * radius) + 1;
	axis->index = g_new (gint, (gsize) n_out * (gsize) axis->n_taps);
	axis->weight = g_new (gfloat, (gsize) n_out * (gsize) axis->n_taps);

	for (o = 0; o < n_out; o++)
	{
		gint   *idx = axis->index + (gsize) o * (gsize) axis->n_taps;
		gfloat *w = axis->weight + (gsize) o * (gsize) axis->n_taps;
		gdouble c = ((gdouble) o + 0.5) * f;
		gint    lo = (gint) floor (c - radius);
		gdouble sum = 0.0;
		gdouble raw[256];
		gint    k;

		/* raw weights in doubles (n_taps <= 2 * 3 * 16 + 1 = 97) */
		for (k = 0; k < axis->n_taps; k++)
		{
			gint s = lo + k;

			raw[k] = filter_kernel (filter, ((gdouble) s + 0.5 - c) / f);
			sum += raw[k];
			idx[k] = CLAMP (s, 0, n_src - 1);
		}

		/* normalise, so constant images stay constant; every kernel
		 * peaks at the centre, which is always among the taps */
		for (k = 0; k < axis->n_taps; k++)
			w[k] = (gfloat) (raw[k] / sum);
	}
}

static void
resample_axis_clear (ResampleAxis *axis)
{
	g_clear_pointer (&axis->index, g_free);
	g_clear_pointer (&axis->weight, g_free);
}

/* ==========================================================================
 * sRGB
 * ========================================================================== */

/* srgb_decode: an 8-bit sRGB value to linear light (IEC 61966-2-1) */
static gfloat
srgb_decode (guint8 b)
{
	gdouble v = (gdouble) b / 255.0;

	if (v <= 0.04045)
		return (gfloat) (v / 12.92);
	return (gfloat) pow ((v + 0.055) / 1.055, 2.4);
}

/* srgb_encode_byte: linear 0..1 to a rounded 8-bit sRGB value */
static guint8
srgb_encode_byte (gfloat linear)
{
	gdouble v = (gdouble) CLAMP (linear, 0.0f, 1.0f);
	gdouble e;

	if (v <= 0.0031308)
		e = v * 12.92;
	else
		e = 1.055 * pow (v, 1.0 / 2.4) - 0.055;
	return (guint8) CLAMP ((gint) (e * 255.0 + 0.5), 0, 255);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

GrlImage *
lrg_image_downsample (GrlImage          *src,
                      guint              factor,
                      LrgResampleFilter  filter)
{
	g_autoptr(GrlImage) converted = NULL;
	g_autofree gfloat  *lin = NULL;
	g_autofree gfloat  *row = NULL;
	g_autofree guint8  *out = NULL;
	ResampleAxis        ax = { 0, NULL, NULL };
	ResampleAxis        ay = { 0, NULL, NULL };
	GrlImage           *pixels_of;
	const guint8       *px;
	gfloat              table[256];
	gint                w, h, ow, oh;
	gint                x, y, k, c;
	gsize               i, n;

	g_return_val_if_fail (GRL_IS_IMAGE (src), NULL);
	g_return_val_if_fail (factor >= 1 && factor <= LRG_IMAGE_DOWNSAMPLE_MAX_FACTOR, NULL);
	g_return_val_if_fail (filter <= LRG_RESAMPLE_FILTER_LANCZOS3, NULL);

	/* factor 1 changes nothing: hand back an exact copy */
	if (factor == 1)
		return grl_image_copy (src);

	w = grl_image_get_width (src);
	h = grl_image_get_height (src);
	if (w < (gint) factor || h < (gint) factor)
	{
		lrg_log_debug ("downsample: %dx%d image is smaller than factor %u", w, h, factor);
		return NULL;
	}
	ow = w / (gint) factor;
	oh = h / (gint) factor;

	/* read RGBA8 bytes; grl_image_get_pixels() converts in place, so
	 * convert a copy rather than the caller's image */
	pixels_of = src;
	if (grl_image_get_format (src) != GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
	{
		converted = grl_image_copy (src);
		pixels_of = converted;
	}
	px = grl_image_get_pixels (pixels_of, NULL);
	if (px == NULL)
		return NULL;

	/* 1-2. decode to linear light and premultiply */
	for (k = 0; k < 256; k++)
		table[k] = srgb_decode ((guint8) k);
	n = (gsize) w * (gsize) h;
	lin = g_new (gfloat, n * 4);
	for (i = 0; i < n; i++)
	{
		gfloat a = (gfloat) px[i * 4 + 3] / 255.0f;

		lin[i * 4 + 0] = table[px[i * 4 + 0]] * a;
		lin[i * 4 + 1] = table[px[i * 4 + 1]] * a;
		lin[i * 4 + 2] = table[px[i * 4 + 2]] * a;
		lin[i * 4 + 3] = a;
	}

	/* 3a. horizontal pass: w x h -> ow x h */
	resample_axis_init (&ax, w, ow, factor, filter);
	resample_axis_init (&ay, h, oh, factor, filter);
	row = g_new0 (gfloat, (gsize) ow * (gsize) h * 4);
	for (y = 0; y < h; y++)
	{
		const gfloat *srow = lin + (gsize) y * (gsize) w * 4;

		for (x = 0; x < ow; x++)
		{
			const gint   *idx = ax.index + (gsize) x * (gsize) ax.n_taps;
			const gfloat *wt = ax.weight + (gsize) x * (gsize) ax.n_taps;
			gfloat       *d = row + ((gsize) y * (gsize) ow + (gsize) x) * 4;
			gfloat        acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			for (k = 0; k < ax.n_taps; k++)
			{
				const gfloat *s = srow + (gsize) idx[k] * 4;

				if (wt[k] == 0.0f)
					continue;
				for (c = 0; c < 4; c++)
					acc[c] += wt[k] * s[c];
			}
			for (c = 0; c < 4; c++)
				d[c] = acc[c];
		}
	}

	/* 3b-5. vertical pass, clamp, unpremultiply, encode */
	out = g_new (guint8, (gsize) ow * (gsize) oh * 4);
	for (y = 0; y < oh; y++)
	{
		const gint   *idx = ay.index + (gsize) y * (gsize) ay.n_taps;
		const gfloat *wt = ay.weight + (gsize) y * (gsize) ay.n_taps;

		for (x = 0; x < ow; x++)
		{
			guint8 *d = out + ((gsize) y * (gsize) ow + (gsize) x) * 4;
			gfloat  acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			gfloat  a;

			for (k = 0; k < ay.n_taps; k++)
			{
				const gfloat *s = row + ((gsize) idx[k] * (gsize) ow + (gsize) x) * 4;

				if (wt[k] == 0.0f)
					continue;
				for (c = 0; c < 4; c++)
					acc[c] += wt[k] * s[c];
			}

			/* 4. clamp the ringing back into a valid premultiplied pixel */
			a = CLAMP (acc[3], 0.0f, 1.0f);
			d[3] = (guint8) CLAMP ((gint) (a * 255.0f + 0.5f), 0, 255);
			if (a <= 0.0f || d[3] == 0)
			{
				d[0] = d[1] = d[2] = 0;
				continue;
			}

			/* 5. unpremultiply and encode */
			for (c = 0; c < 3; c++)
				d[c] = srgb_encode_byte (CLAMP (acc[c], 0.0f, a) / a);
		}
	}

	resample_axis_clear (&ax);
	resample_axis_clear (&ay);

	return grl_image_new_from_pixels (ow, oh, GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, out);
}
