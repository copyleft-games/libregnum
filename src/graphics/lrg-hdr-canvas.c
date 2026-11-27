/* lrg-hdr-canvas.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrCanvas - a float, linear-light, premultiplied RGBA canvas.
 *
 * Storage is premultiplied because every operation on it is then a
 * linear one: source over is dst = src + dst (1 - src.a), the average of
 * supersamples is the plain mean, and adding light is a plain sum.  The
 * public per-pixel API speaks straight alpha (the way people specify
 * colours) and converts at the boundary.  The tone-map export
 * unpremultiplies first, because the curves are non-linear and must see
 * the colour of the surface, not colour times coverage.
 */

#include "config.h"

#define LRG_LOG_DOMAIN "Libregnum-Graphics"
#include "lrg-hdr-canvas.h"
#include "../lrg-log.h"
#include <math.h>
#include <string.h>

struct _LrgHdrCanvas
{
	GObject  parent_instance;

	guint    width;
	guint    height;
	gfloat  *data;      /* width x height x 4, premultiplied RGBA */
};

G_DEFINE_FINAL_TYPE (LrgHdrCanvas, lrg_hdr_canvas, G_TYPE_OBJECT)

/* ==========================================================================
 * Helpers
 * ========================================================================== */

/* clamp01: alpha into 0..1, NaN to 0 (the comparisons fail for NaN) */
static gfloat
clamp01 (gfloat a)
{
	if (a > 0.0f)
		return a < 1.0f ? a : 1.0f;
	return 0.0f;
}

/* pixel_at: the four floats of (x, y), or NULL outside the canvas */
static gfloat *
pixel_at (LrgHdrCanvas *self,
          gint          x,
          gint          y)
{
	if (x < 0 || y < 0 || (guint) x >= self->width || (guint) y >= self->height)
		return NULL;
	return self->data + ((gsize) y * (gsize) self->width + (gsize) x) * 4;
}

/* over: composite a premultiplied colour over a premultiplied pixel */
static void
over (gfloat       *dst,
      const gfloat *src)
{
	gfloat k = 1.0f - src[3];

	dst[0] = src[0] + dst[0] * k;
	dst[1] = src[1] + dst[1] * k;
	dst[2] = src[2] + dst[2] * k;
	dst[3] = src[3] + dst[3] * k;
}

/* srgb_encode: linear 0..1 to sRGB 0..1 (IEC 61966-2-1) */
static gfloat
srgb_encode (gfloat v)
{
	v = clamp01 (v);
	if (v <= 0.0031308f)
		return v * 12.92f;
	return 1.055f * powf (v, 1.0f / 2.4f) - 0.055f;
}

/* dither_offset: a deterministic value in [-0.5, 0.5) per pixel and
 * channel, from an integer hash (lowbias32 by Chris Wellons): white
 * noise, so no ordered pattern shows in the gradients it breaks up */
static gfloat
dither_offset (guint x,
               guint y,
               guint c)
{
	guint32 h;

	h = (guint32) x * 0x8da6b343u ^ (guint32) y * 0xd8163841u ^ (guint32) (c + 1) * 0xcb1ab31fu;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return (gfloat) (h >> 8) * (1.0f / 16777216.0f) - 0.5f;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_hdr_canvas_finalize (GObject *object)
{
	LrgHdrCanvas *self = LRG_HDR_CANVAS (object);

	g_clear_pointer (&self->data, g_free);

	G_OBJECT_CLASS (lrg_hdr_canvas_parent_class)->finalize (object);
}

static void
lrg_hdr_canvas_class_init (LrgHdrCanvasClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_hdr_canvas_finalize;
}

static void
lrg_hdr_canvas_init (LrgHdrCanvas *self)
{
	self->width = 0;
	self->height = 0;
	self->data = NULL;
}

/* ==========================================================================
 * Construction and access
 * ========================================================================== */

LrgHdrCanvas *
lrg_hdr_canvas_new (guint width,
                    guint height)
{
	LrgHdrCanvas *self;

	g_return_val_if_fail (width >= 1 && width <= LRG_HDR_CANVAS_MAX_SIZE, NULL);
	g_return_val_if_fail (height >= 1 && height <= LRG_HDR_CANVAS_MAX_SIZE, NULL);

	self = g_object_new (LRG_TYPE_HDR_CANVAS, NULL);
	self->width = width;
	self->height = height;
	self->data = g_new0 (gfloat, (gsize) width * (gsize) height * 4);

	return self;
}

LrgHdrCanvas *
lrg_hdr_canvas_new_from_hdr_image (LrgHdrImage *image)
{
	LrgHdrCanvas *self;
	const gfloat *rgb;
	gsize         i, n;

	g_return_val_if_fail (LRG_IS_HDR_IMAGE (image), NULL);

	/* LrgHdrImage clamps its own size into 1..16384, the same range */
	self = lrg_hdr_canvas_new (lrg_hdr_image_get_width (image),
	                           lrg_hdr_image_get_height (image));
	rgb = lrg_hdr_image_get_data (image, NULL);

	/* opaque: premultiplied by alpha 1 is the colour itself */
	n = (gsize) self->width * (gsize) self->height;
	for (i = 0; i < n; i++)
	{
		self->data[i * 4 + 0] = rgb[i * 3 + 0];
		self->data[i * 4 + 1] = rgb[i * 3 + 1];
		self->data[i * 4 + 2] = rgb[i * 3 + 2];
		self->data[i * 4 + 3] = 1.0f;
	}

	return self;
}

guint
lrg_hdr_canvas_get_width (LrgHdrCanvas *self)
{
	g_return_val_if_fail (LRG_IS_HDR_CANVAS (self), 0);

	return self->width;
}

guint
lrg_hdr_canvas_get_height (LrgHdrCanvas *self)
{
	g_return_val_if_fail (LRG_IS_HDR_CANVAS (self), 0);

	return self->height;
}

gfloat *
lrg_hdr_canvas_get_data (LrgHdrCanvas *self,
                         gsize        *n_floats)
{
	g_return_val_if_fail (LRG_IS_HDR_CANVAS (self), NULL);

	if (n_floats != NULL)
		*n_floats = (gsize) self->width * (gsize) self->height * 4;
	return self->data;
}

/* ==========================================================================
 * Per-pixel operations
 * ========================================================================== */

void
lrg_hdr_canvas_clear (LrgHdrCanvas *self,
                      gfloat        r,
                      gfloat        g,
                      gfloat        b,
                      gfloat        a)
{
	gfloat pm[4];
	gsize  i, n;

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));

	/* premultiply once, then stamp it everywhere */
	pm[3] = clamp01 (a);
	pm[0] = r * pm[3];
	pm[1] = g * pm[3];
	pm[2] = b * pm[3];
	n = (gsize) self->width * (gsize) self->height;
	for (i = 0; i < n; i++)
		memcpy (self->data + i * 4, pm, sizeof pm);
}

void
lrg_hdr_canvas_set_pixel (LrgHdrCanvas *self,
                          gint          x,
                          gint          y,
                          gfloat        r,
                          gfloat        g,
                          gfloat        b,
                          gfloat        a)
{
	gfloat *p;

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));

	p = pixel_at (self, x, y);
	if (p == NULL)
		return;
	p[3] = clamp01 (a);
	p[0] = r * p[3];
	p[1] = g * p[3];
	p[2] = b * p[3];
}

void
lrg_hdr_canvas_get_pixel (LrgHdrCanvas *self,
                          gint          x,
                          gint          y,
                          gfloat       *out_rgba)
{
	const gfloat *p;

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));
	g_return_if_fail (out_rgba != NULL);

	p = pixel_at (self, x, y);
	if (p == NULL || p[3] <= 0.0f)
	{
		/* outside, or fully transparent: no colour to recover */
		out_rgba[0] = out_rgba[1] = out_rgba[2] = out_rgba[3] = 0.0f;
		return;
	}
	out_rgba[0] = p[0] / p[3];
	out_rgba[1] = p[1] / p[3];
	out_rgba[2] = p[2] / p[3];
	out_rgba[3] = p[3];
}

void
lrg_hdr_canvas_blend_over (LrgHdrCanvas *self,
                           gint          x,
                           gint          y,
                           gfloat        r,
                           gfloat        g,
                           gfloat        b,
                           gfloat        a)
{
	gfloat *p;
	gfloat  src[4];

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));

	p = pixel_at (self, x, y);
	if (p == NULL)
		return;
	src[3] = clamp01 (a);
	src[0] = r * src[3];
	src[1] = g * src[3];
	src[2] = b * src[3];
	over (p, src);
}

void
lrg_hdr_canvas_add (LrgHdrCanvas *self,
                    gint          x,
                    gint          y,
                    gfloat        r,
                    gfloat        g,
                    gfloat        b)
{
	gfloat *p;

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));

	p = pixel_at (self, x, y);
	if (p == NULL)
		return;
	p[0] += r;
	p[1] += g;
	p[2] += b;
}

/* ==========================================================================
 * Shading
 * ========================================================================== */

void
lrg_hdr_canvas_shade (LrgHdrCanvas    *self,
                      LrgHdrShadeFunc  func,
                      gpointer         user_data,
                      guint            samples)
{
	gfloat inv_n, inv_count;
	guint  px, py, sx, sy;

	g_return_if_fail (LRG_IS_HDR_CANVAS (self));
	g_return_if_fail (func != NULL);

	samples = CLAMP (samples, 1u, (guint) LRG_HDR_CANVAS_MAX_SAMPLES);
	inv_n = 1.0f / (gfloat) samples;
	inv_count = inv_n * inv_n;

	for (py = 0; py < self->height; py++)
	{
		for (px = 0; px < self->width; px++)
		{
			gfloat acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

			/* the stratified grid: the centre of each of the n x n
			 * cells of the pixel; with n = 1 that is the pixel centre */
			for (sy = 0; sy < samples; sy++)
			{
				for (sx = 0; sx < samples; sx++)
				{
					gfloat c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					gfloat a;

					func ((gfloat) px + ((gfloat) sx + 0.5f) * inv_n,
					      (gfloat) py + ((gfloat) sy + 0.5f) * inv_n,
					      c, user_data);

					/* average premultiplied, so uncovered samples
					 * add coverage 0 without tinting the colour */
					a = clamp01 (c[3]);
					acc[0] += c[0] * a;
					acc[1] += c[1] * a;
					acc[2] += c[2] * a;
					acc[3] += a;
				}
			}

			acc[0] *= inv_count;
			acc[1] *= inv_count;
			acc[2] *= inv_count;
			acc[3] *= inv_count;
			over (self->data + ((gsize) py * (gsize) self->width + (gsize) px) * 4, acc);
		}
	}
}

/* ==========================================================================
 * Export
 * ========================================================================== */

GrlImage *
lrg_hdr_canvas_to_image (LrgHdrCanvas       *self,
                         LrgTonemapOperator  op,
                         gfloat              exposure,
                         gboolean            dither)
{
	g_autofree guint8 *out = NULL;
	gfloat             scale;
	guint              x, y, c;

	g_return_val_if_fail (LRG_IS_HDR_CANVAS (self), NULL);
	g_return_val_if_fail (op <= LRG_TONEMAP_OPERATOR_ACES, NULL);

	/* exposure in stops, the convention of LrgTonemap:exposure */
	if (!isfinite (exposure))
		exposure = 0.0f;
	scale = exp2f (CLAMP (exposure, -64.0f, 64.0f));

	out = g_new (guint8, (gsize) self->width * (gsize) self->height * 4);
	for (y = 0; y < self->height; y++)
	{
		for (x = 0; x < self->width; x++)
		{
			gsize         i = (gsize) y * (gsize) self->width + (gsize) x;
			const gfloat *p = self->data + i * 4;
			guint8       *d = out + i * 4;
			gfloat        a = clamp01 (p[3]);
			gfloat        rgb[3];

			/* fully transparent: nothing to tone-map */
			if (a <= 0.0f)
			{
				d[0] = d[1] = d[2] = d[3] = 0;
				continue;
			}

			/* unpremultiply and expose: the curve sees surface colour */
			for (c = 0; c < 3; c++)
				rgb[c] = p[c] / a * scale;

			/* the operator curve shared with LrgTonemap, 0..1 linear */
			lrg_tonemap_operator_apply_rgb (op, LRG_HDR_CANVAS_WHITE_POINT, rgb, rgb);

			/* sRGB encode, optional dither, quantise */
			for (c = 0; c < 3; c++)
			{
				gfloat v = srgb_encode (rgb[c]) * 255.0f + 0.5f;

				if (dither)
					v += dither_offset (x, y, c);
				d[c] = (guint8) CLAMP ((gint) floorf (v), 0, 255);
			}
			d[3] = (guint8) CLAMP ((gint) (a * 255.0f + 0.5f), 0, 255);
		}
	}

	return grl_image_new_from_pixels ((gint) self->width, (gint) self->height,
	                                  GRL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, out);
}

LrgHdrImage *
lrg_hdr_canvas_to_hdr_image (LrgHdrCanvas *self)
{
	LrgHdrImage *image;
	gfloat      *rgb;
	gsize        i, n;

	g_return_val_if_fail (LRG_IS_HDR_CANVAS (self), NULL);

	/* the premultiplied colour is the canvas composited over black */
	image = lrg_hdr_image_new (self->width, self->height);
	rgb = lrg_hdr_image_get_data (image, NULL);
	n = (gsize) self->width * (gsize) self->height;
	for (i = 0; i < n; i++)
	{
		rgb[i * 3 + 0] = self->data[i * 4 + 0];
		rgb[i * 3 + 1] = self->data[i * 4 + 1];
		rgb[i * 3 + 2] = self->data[i * 4 + 2];
	}

	return image;
}
