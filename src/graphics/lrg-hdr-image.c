/* lrg-hdr-image.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHdrImage - Radiance RGBE (.hdr) reader and writer, written from the
 * format description in Greg Ward's "Real Pixels" (Graphics Gems II) and
 * the Radiance file format documentation.
 */

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lrg-hdr-image.h"

struct _LrgHdrImage
{
	GObject  parent_instance;

	guint    width;
	guint    height;
	gfloat   exposure;
	gfloat  *pixels;	/* width * height * 3, rows from the top */
};

G_DEFINE_FINAL_TYPE (LrgHdrImage, lrg_hdr_image, G_TYPE_OBJECT)

G_DEFINE_QUARK (lrg-hdr-image-error-quark, lrg_hdr_image_error)

GType
lrg_hdr_image_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_HDR_IMAGE_ERROR_FAILED, "LRG_HDR_IMAGE_ERROR_FAILED", "failed" },
			{ LRG_HDR_IMAGE_ERROR_IO, "LRG_HDR_IMAGE_ERROR_IO", "io" },
			{ LRG_HDR_IMAGE_ERROR_FORMAT, "LRG_HDR_IMAGE_ERROR_FORMAT", "format" },
			{ LRG_HDR_IMAGE_ERROR_TRUNCATED, "LRG_HDR_IMAGE_ERROR_TRUNCATED", "truncated" },
			{ LRG_HDR_IMAGE_ERROR_SIZE, "LRG_HDR_IMAGE_ERROR_SIZE", "size" },
			{ 0, NULL, NULL }
		};
		GType id = g_enum_register_static (g_intern_static_string ("LrgHdrImageError"), values);

		g_once_init_leave (&type_id, id);
	}
	return (GType)type_id;
}

/* ==========================================================================
 * GObject
 * ========================================================================== */

static void
lrg_hdr_image_finalize (GObject *object)
{
	LrgHdrImage *self = LRG_HDR_IMAGE (object);

	g_clear_pointer (&self->pixels, g_free);
	G_OBJECT_CLASS (lrg_hdr_image_parent_class)->finalize (object);
}

static void
lrg_hdr_image_class_init (LrgHdrImageClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = lrg_hdr_image_finalize;
}

static void
lrg_hdr_image_init (LrgHdrImage *self)
{
	self->exposure = 1.0f;
}

/* Allocates a zeroed image of a size already validated by the caller. */
static LrgHdrImage *
image_alloc (guint width,
             guint height)
{
	LrgHdrImage *self = g_object_new (LRG_TYPE_HDR_IMAGE, NULL);

	self->width = width;
	self->height = height;
	self->pixels = g_new0 (gfloat, (gsize)width * (gsize)height * 3);
	return self;
}

LrgHdrImage *
lrg_hdr_image_new (guint width,
                   guint height)
{
	return image_alloc (CLAMP (width, 1u, (guint)LRG_HDR_IMAGE_MAX_SIZE),
	                    CLAMP (height, 1u, (guint)LRG_HDR_IMAGE_MAX_SIZE));
}

LrgHdrImage *
lrg_hdr_image_new_from_data (guint         width,
                             guint         height,
                             const gfloat *rgb)
{
	LrgHdrImage *self;

	g_return_val_if_fail (rgb != NULL, NULL);

	self = lrg_hdr_image_new (width, height);
	/* only copy what both sides have (the size may have been clamped) */
	if (self->width == width && self->height == height)
		memcpy (self->pixels, rgb, sizeof (gfloat) * (gsize)width * (gsize)height * 3);
	return self;
}

/* ==========================================================================
 * RGBE pixels
 * ========================================================================== */

void
lrg_hdr_rgbe_encode (const gfloat *rgb,
                     guint8       *out_rgbe)
{
	gfloat c[3];
	gfloat v;
	gint   e;
	gint   i;
	gdouble m;
	gdouble scale;

	g_return_if_fail (rgb != NULL);
	g_return_if_fail (out_rgbe != NULL);

	/* NaN, infinities and negatives cannot be stored: clamp to 0 */
	for (i = 0; i < 3; i++)
		c[i] = (isfinite (rgb[i]) && rgb[i] > 0.0f) ? rgb[i] : 0.0f;

	v = MAX (c[0], MAX (c[1], c[2]));
	if (v < 1e-32f)
	{
		out_rgbe[0] = out_rgbe[1] = out_rgbe[2] = out_rgbe[3] = 0;
		return;
	}

	/* v = m * 2^e with m in [0.5, 1): mantissas are c * 256 / 2^e */
	m = frexp ((gdouble)v, &e);
	scale = m * 256.0 / (gdouble)v;
	for (i = 0; i < 3; i++)
	{
		gdouble b = (gdouble)c[i] * scale;

		out_rgbe[i] = (guint8)CLAMP (b, 0.0, 255.0);
	}
	out_rgbe[3] = (guint8)CLAMP (e + 128, 0, 255);
}

void
lrg_hdr_rgbe_decode (const guint8 *rgbe,
                     gfloat       *out_rgb)
{
	gdouble f;
	gint    i;

	g_return_if_fail (rgbe != NULL);
	g_return_if_fail (out_rgb != NULL);

	if (rgbe[3] == 0)
	{
		out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
		return;
	}
	/* the +0.5 recentres the truncation of the encoder (Radiance
	 * colr_color); a zero mantissa stays an exact zero */
	f = ldexp (1.0, (gint)rgbe[3] - (128 + 8));
	for (i = 0; i < 3; i++)
		out_rgb[i] = rgbe[i] == 0 ? 0.0f : (gfloat)(((gdouble)rgbe[i] + 0.5) * f);
}

/* ==========================================================================
 * Reader
 * ========================================================================== */

typedef struct
{
	const guint8 *data;
	gsize         len;
	gsize         pos;
} Reader;

/* Reads one header line (without its newline) into a new string, NULL at
 * the end of the data. */
static gchar *
read_line (Reader *r)
{
	gsize start = r->pos;
	gchar *line;

	if (r->pos >= r->len)
		return NULL;
	while (r->pos < r->len && r->data[r->pos] != '\n')
		r->pos++;
	line = g_strndup ((const gchar *)r->data + start, r->pos - start);
	if (r->pos < r->len)
		r->pos++;	/* the newline */
	return line;
}

/*
 * read_scanline:
 * Decodes one scanline of @width RGBE pixels into @out (width * 4 bytes),
 * in whichever of the three encodings it uses.  An old-style repeat
 * record may only repeat a pixel of the same scanline.
 */
static gboolean
read_scanline (Reader   *r,
               guint     width,
               guint8   *out,
               GError  **error)
{
	guint x;

	/* the adaptive (new) run-length scheme: 2, 2, width (big endian) */
	if (width >= 8 && width < 0x8000 && r->pos + 4 <= r->len &&
	    r->data[r->pos] == 2 && r->data[r->pos + 1] == 2 && (r->data[r->pos + 2] & 0x80) == 0)
	{
		guint w = ((guint)r->data[r->pos + 2] << 8) | (guint)r->data[r->pos + 3];
		guint ch;

		if (w != width)
		{
			g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_FORMAT,
			             "Run-length scanline width %u does not match the image width %u",
			             w, width);
			return FALSE;
		}
		r->pos += 4;
		/* each of the four channels is coded separately: runs (count > 128)
		 * repeat one byte, literals (count <= 128) copy bytes */
		for (ch = 0; ch < 4; ch++)
		{
			x = 0;
			while (x < width)
			{
				guint count;

				if (r->pos >= r->len)
					goto truncated;
				count = r->data[r->pos++];
				if (count > 128)
				{
					guint8 v;
					guint  k;

					count -= 128;
					if (count == 0 || x + count > width || r->pos >= r->len)
						goto truncated;
					v = r->data[r->pos++];
					for (k = 0; k < count; k++)
						out[(x + k) * 4 + ch] = v;
					x += count;
				}
				else
				{
					guint k;

					if (count == 0 || x + count > width || r->pos + count > r->len)
						goto truncated;
					for (k = 0; k < count; k++)
						out[(x + k) * 4 + ch] = r->data[r->pos++];
					x += count;
				}
			}
		}
		return TRUE;
	}

	/* flat pixels, possibly with the old run-length records (1, 1, 1, n:
	 * repeat the previous pixel n << shift times, consecutive records
	 * shifting by 8 more bits) */
	{
		guint shift = 0;

		x = 0;
		while (x < width)
		{
			const guint8 *p;

			if (r->pos + 4 > r->len)
				goto truncated;
			p = r->data + r->pos;
			r->pos += 4;
			if (p[0] == 1 && p[1] == 1 && p[2] == 1)
			{
				guint count = (guint)p[3] << shift;
				guint k;

				if (x == 0 || x + count > width || shift > 16)
					goto truncated;
				for (k = 0; k < count; k++)
					memcpy (out + (x + k) * 4, out + (x - 1) * 4, 4);
				x += count;
				shift += 8;
			}
			else
			{
				memcpy (out + x * 4, p, 4);
				x++;
				shift = 0;
			}
		}
	}
	return TRUE;

truncated:
	g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_TRUNCATED,
	             "The pixel data is truncated or a run overflows its scanline");
	return FALSE;
}

LrgHdrImage *
lrg_hdr_image_new_from_bytes (GBytes  *data,
                              GError **error)
{
	g_autoptr(LrgHdrImage) self = NULL;
	g_autofree guint8     *line = NULL;
	Reader                 r;
	gboolean               format_ok = TRUE;
	gboolean               flip = FALSE;
	gfloat                 exposure = 1.0f;
	guint                  width = 0;
	guint                  height = 0;
	guint                  y;
	gsize                  len;

	g_return_val_if_fail (data != NULL, NULL);

	r.data = g_bytes_get_data (data, &len);
	r.len = len;
	r.pos = 0;

	/* magic line: "#?RADIANCE" (or "#?RGBE" from some writers) */
	{
		g_autofree gchar *magic = read_line (&r);

		if (magic == NULL || !g_str_has_prefix (magic, "#?"))
		{
			g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_FORMAT,
			             "Not a Radiance picture (missing the #? signature)");
			return NULL;
		}
	}

	/* header variables up to the blank line */
	for (;;)
	{
		g_autofree gchar *hl = read_line (&r);

		if (hl == NULL)
		{
			g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_TRUNCATED,
			             "The header has no end");
			return NULL;
		}
		g_strchomp (hl);
		if (*hl == '\0')
			break;
		if (g_str_has_prefix (hl, "FORMAT="))
			format_ok = g_strcmp0 (hl + 7, "32-bit_rle_rgbe") == 0;
		else if (g_str_has_prefix (hl, "EXPOSURE="))
		{
			gdouble e = g_ascii_strtod (hl + 9, NULL);

			if (e > 0.0 && isfinite (e))
				exposure *= (gfloat)e;
		}
	}
	if (!format_ok)
	{
		g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_FORMAT,
		             "Only 32-bit_rle_rgbe pictures are supported");
		return NULL;
	}

	/* resolution: "-Y H +X W" (top-down) or "+Y H +X W" (bottom-up) */
	{
		g_autofree gchar *res = read_line (&r);
		gchar  ysign;
		gchar  xsign;
		gchar  yaxis;
		gchar  xaxis;
		gint64 h = 0;
		gint64 w = 0;

		if (res == NULL ||
		    sscanf (res, "%c%c %" G_GINT64_FORMAT " %c%c %" G_GINT64_FORMAT,
		            &ysign, &yaxis, &h, &xsign, &xaxis, &w) != 6 ||
		    yaxis != 'Y' || xaxis != 'X' || xsign != '+' ||
		    (ysign != '-' && ysign != '+'))
		{
			g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_FORMAT,
			             "Unsupported resolution line (only -Y/+Y H +X W)");
			return NULL;
		}
		if (w <= 0 || h <= 0 || w > LRG_HDR_IMAGE_MAX_SIZE || h > LRG_HDR_IMAGE_MAX_SIZE)
		{
			g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_SIZE,
			             "Image size %" G_GINT64_FORMAT "x%" G_GINT64_FORMAT " is out of range",
			             w, h);
			return NULL;
		}
		width = (guint)w;
		height = (guint)h;
		flip = ysign == '+';
	}

	/* scanlines, each decoded to RGBE bytes then to floats */
	self = image_alloc (width, height);
	self->exposure = exposure;
	line = g_new0 (guint8, (gsize)width * 4);
	for (y = 0; y < height; y++)
	{
		guint   row = flip ? height - 1 - y : y;
		gfloat *dst = self->pixels + (gsize)row * width * 3;
		guint   x;

		if (!read_scanline (&r, width, line, error))
			return NULL;
		for (x = 0; x < width; x++)
		{
			lrg_hdr_rgbe_decode (line + x * 4, dst + x * 3);
			if (exposure != 1.0f)
			{
				dst[x * 3 + 0] /= exposure;
				dst[x * 3 + 1] /= exposure;
				dst[x * 3 + 2] /= exposure;
			}
		}
	}
	return g_steal_pointer (&self);
}

LrgHdrImage *
lrg_hdr_image_new_from_file (const gchar  *path,
                             GError      **error)
{
	g_autoptr(GBytes) bytes = NULL;
	g_autofree gchar *contents = NULL;
	gsize             len = 0;
	GError           *io_error = NULL;

	g_return_val_if_fail (path != NULL, NULL);

	if (!g_file_get_contents (path, &contents, &len, &io_error))
	{
		g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_IO,
		             "Cannot read %s: %s", path, io_error->message);
		g_error_free (io_error);
		return NULL;
	}
	bytes = g_bytes_new_take (g_steal_pointer (&contents), len);
	return lrg_hdr_image_new_from_bytes (bytes, error);
}

/* ==========================================================================
 * Accessors and sampling
 * ========================================================================== */

guint
lrg_hdr_image_get_width (LrgHdrImage *self)
{
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), 0);
	return self->width;
}

guint
lrg_hdr_image_get_height (LrgHdrImage *self)
{
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), 0);
	return self->height;
}

gfloat
lrg_hdr_image_get_exposure (LrgHdrImage *self)
{
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), 1.0f);
	return self->exposure;
}

gfloat *
lrg_hdr_image_get_data (LrgHdrImage *self,
                        gsize       *n_floats)
{
	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), NULL);

	if (n_floats != NULL)
		*n_floats = (gsize)self->width * (gsize)self->height * 3;
	return self->pixels;
}

void
lrg_hdr_image_get_pixel (LrgHdrImage *self,
                         guint        x,
                         guint        y,
                         gfloat      *out_rgb)
{
	const gfloat *p;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));
	g_return_if_fail (out_rgb != NULL);

	x = MIN (x, self->width - 1);
	y = MIN (y, self->height - 1);
	p = self->pixels + ((gsize)y * self->width + x) * 3;
	out_rgb[0] = p[0];
	out_rgb[1] = p[1];
	out_rgb[2] = p[2];
}

void
lrg_hdr_image_set_pixel (LrgHdrImage  *self,
                         guint         x,
                         guint         y,
                         const gfloat *rgb)
{
	gfloat *p;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));
	g_return_if_fail (rgb != NULL);

	if (x >= self->width || y >= self->height)
		return;
	p = self->pixels + ((gsize)y * self->width + x) * 3;
	p[0] = rgb[0];
	p[1] = rgb[1];
	p[2] = rgb[2];
}

void
lrg_hdr_image_sample (LrgHdrImage *self,
                      gfloat       u,
                      gfloat       v,
                      gfloat      *out_rgb)
{
	gfloat  fx;
	gfloat  fy;
	gfloat  tx;
	gfloat  ty;
	gint    x0;
	gint    y0;
	gint    x1;
	gint    y1;
	gint    c;
	const gfloat *p00;
	const gfloat *p10;
	const gfloat *p01;
	const gfloat *p11;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));
	g_return_if_fail (out_rgb != NULL);

	if (!isfinite (u))
		u = 0.0f;
	if (!isfinite (v))
		v = 0.0f;

	/* pixel centres at (i + 0.5) / size: shift by half a pixel first */
	fx = u * (gfloat)self->width - 0.5f;
	fy = CLAMP (v, 0.0f, 1.0f) * (gfloat)self->height - 0.5f;
	x0 = (gint)floorf (fx);
	y0 = (gint)floorf (fy);
	tx = fx - (gfloat)x0;
	ty = fy - (gfloat)y0;

	/* wrap horizontally, clamp vertically */
	x0 = ((x0 % (gint)self->width) + (gint)self->width) % (gint)self->width;
	x1 = (x0 + 1) % (gint)self->width;
	y1 = CLAMP (y0 + 1, 0, (gint)self->height - 1);
	y0 = CLAMP (y0, 0, (gint)self->height - 1);

	p00 = self->pixels + ((gsize)y0 * self->width + (gsize)x0) * 3;
	p10 = self->pixels + ((gsize)y0 * self->width + (gsize)x1) * 3;
	p01 = self->pixels + ((gsize)y1 * self->width + (gsize)x0) * 3;
	p11 = self->pixels + ((gsize)y1 * self->width + (gsize)x1) * 3;
	for (c = 0; c < 3; c++)
	{
		gfloat top = p00[c] + (p10[c] - p00[c]) * tx;
		gfloat bot = p01[c] + (p11[c] - p01[c]) * tx;

		out_rgb[c] = top + (bot - top) * ty;
	}
}

void
lrg_hdr_image_direction_to_uv (const gfloat *direction,
                               gfloat       *out_u,
                               gfloat       *out_v)
{
	gfloat len;
	gfloat y;

	g_return_if_fail (direction != NULL);
	g_return_if_fail (out_u != NULL);
	g_return_if_fail (out_v != NULL);

	len = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] +
	             direction[2] * direction[2]);
	if (len <= 0.0f)
	{
		*out_u = 0.5f;
		*out_v = 0.5f;
		return;
	}
	y = CLAMP (direction[1] / len, -1.0f, 1.0f);
	/* -Z at the centre, +X to the right of it */
	*out_u = 0.5f + atan2f (direction[0], -direction[2]) / (2.0f * (gfloat)G_PI);
	*out_v = acosf (y) / (gfloat)G_PI;
}

void
lrg_hdr_image_sample_direction (LrgHdrImage  *self,
                                const gfloat *direction,
                                gfloat       *out_rgb)
{
	gfloat u;
	gfloat v;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));
	g_return_if_fail (direction != NULL);
	g_return_if_fail (out_rgb != NULL);

	lrg_hdr_image_direction_to_uv (direction, &u, &v);
	lrg_hdr_image_sample (self, u, v, out_rgb);
}

void
lrg_hdr_image_get_average (LrgHdrImage *self,
                           gfloat      *out_rgb)
{
	gdouble sum[3] = { 0.0, 0.0, 0.0 };
	gsize   n;
	gsize   i;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));
	g_return_if_fail (out_rgb != NULL);

	n = (gsize)self->width * self->height;
	for (i = 0; i < n; i++)
	{
		sum[0] += self->pixels[i * 3 + 0];
		sum[1] += self->pixels[i * 3 + 1];
		sum[2] += self->pixels[i * 3 + 2];
	}
	out_rgb[0] = (gfloat)(sum[0] / (gdouble)n);
	out_rgb[1] = (gfloat)(sum[1] / (gdouble)n);
	out_rgb[2] = (gfloat)(sum[2] / (gdouble)n);
}

void
lrg_hdr_image_scale (LrgHdrImage *self,
                     gfloat       factor)
{
	gsize n;
	gsize i;

	g_return_if_fail (LRG_IS_HDR_IMAGE (self));

	n = (gsize)self->width * self->height * 3;
	for (i = 0; i < n; i++)
		self->pixels[i] *= factor;
}

LrgHdrImage *
lrg_hdr_image_resize (LrgHdrImage *self,
                      guint        width,
                      guint        height)
{
	LrgHdrImage *out;
	guint        x;
	guint        y;

	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), NULL);

	out = lrg_hdr_image_new (width, height);
	width = out->width;
	height = out->height;

	/* whole-factor shrink: a box filter keeps the energy of every pixel */
	if (self->width % width == 0 && self->height % height == 0)
	{
		guint fx = self->width / width;
		guint fy = self->height / height;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				gdouble sum[3] = { 0.0, 0.0, 0.0 };
				guint   i;
				guint   j;
				gfloat *dst = out->pixels + ((gsize)y * width + x) * 3;

				for (j = 0; j < fy; j++)
				{
					for (i = 0; i < fx; i++)
					{
						const gfloat *p = self->pixels +
							((gsize)(y * fy + j) * self->width + (x * fx + i)) * 3;

						sum[0] += p[0];
						sum[1] += p[1];
						sum[2] += p[2];
					}
				}
				dst[0] = (gfloat)(sum[0] / (gdouble)(fx * fy));
				dst[1] = (gfloat)(sum[1] / (gdouble)(fx * fy));
				dst[2] = (gfloat)(sum[2] / (gdouble)(fx * fy));
			}
		}
		return out;
	}

	/* otherwise bilinear at the new pixel centres */
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			lrg_hdr_image_sample (self,
			                      ((gfloat)x + 0.5f) / (gfloat)width,
			                      ((gfloat)y + 0.5f) / (gfloat)height,
			                      out->pixels + ((gsize)y * width + x) * 3);
		}
	}
	return out;
}

/* ==========================================================================
 * Writer
 * ========================================================================== */

/*
 * write_rle_channel:
 * Run-length encodes @n bytes of one channel (stride 4) into @out the way
 * Radiance does: runs of 4 or more equal bytes become (128 + count, byte),
 * everything else is copied in literal records of at most 128 bytes.  A
 * short run (2 or 3) directly before a long one is folded into a run of
 * its own when it starts the pending literal stretch.
 */
static void
write_rle_channel (GByteArray   *out,
                   const guint8 *data,
                   guint         n)
{
	guint cur = 0;

#define CH(i) (data[(gsize)(i) * 4])

	while (cur < n)
	{
		guint beg_run = cur;
		guint run_count = 0;
		guint old_run_count = 0;

		/* find the next run of at least 4 */
		while (run_count < 4 && beg_run < n)
		{
			beg_run += run_count;
			old_run_count = run_count;
			run_count = 1;
			while (beg_run + run_count < n && run_count < 127 &&
			       CH (beg_run) == CH (beg_run + run_count))
				run_count++;
		}
		/* a short run right at the start of the stretch: emit it as a run */
		if (old_run_count > 1 && old_run_count == beg_run - cur)
		{
			guint8 rec[2];

			rec[0] = (guint8)(128 + old_run_count);
			rec[1] = CH (cur);
			g_byte_array_append (out, rec, 2);
			cur = beg_run;
		}
		/* literals up to the run */
		while (cur < beg_run)
		{
			guint  nonrun = MIN (128u, beg_run - cur);
			guint8 count = (guint8)nonrun;
			guint  k;

			g_byte_array_append (out, &count, 1);
			for (k = 0; k < nonrun; k++)
				g_byte_array_append (out, &CH (cur + k), 1);
			cur += nonrun;
		}
		/* the run itself */
		if (run_count >= 4)
		{
			guint8 rec[2];

			rec[0] = (guint8)(128 + run_count);
			rec[1] = CH (beg_run);
			g_byte_array_append (out, rec, 2);
			cur += run_count;
		}
	}
#undef CH
}

GBytes *
lrg_hdr_image_save_to_bytes (LrgHdrImage *self)
{
	g_autoptr(GByteArray) out = NULL;
	g_autofree guint8    *line = NULL;
	g_autofree gchar     *header = NULL;
	gboolean              rle;
	guint                 x;
	guint                 y;

	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), NULL);

	out = g_byte_array_new ();
	header = g_strdup_printf ("#?RADIANCE\n# written by libregnum\nFORMAT=32-bit_rle_rgbe\n\n-Y %u +X %u\n",
	                          self->height, self->width);
	g_byte_array_append (out, (const guint8 *)header, (guint)strlen (header));

	rle = self->width >= 8 && self->width < 0x8000;
	line = g_new0 (guint8, (gsize)self->width * 4);
	for (y = 0; y < self->height; y++)
	{
		const gfloat *src = self->pixels + (gsize)y * self->width * 3;

		for (x = 0; x < self->width; x++)
			lrg_hdr_rgbe_encode (src + x * 3, line + x * 4);

		if (rle)
		{
			guint8 tag[4];
			guint  ch;

			tag[0] = 2;
			tag[1] = 2;
			tag[2] = (guint8)(self->width >> 8);
			tag[3] = (guint8)(self->width & 0xff);
			g_byte_array_append (out, tag, 4);
			for (ch = 0; ch < 4; ch++)
				write_rle_channel (out, line + ch, self->width);
		}
		else
		{
			g_byte_array_append (out, line, self->width * 4);
		}
	}
	return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}

gboolean
lrg_hdr_image_save_to_file (LrgHdrImage  *self,
                            const gchar  *path,
                            GError      **error)
{
	g_autoptr(GBytes) bytes = NULL;
	GError           *io_error = NULL;
	gconstpointer     data;
	gsize             len;

	g_return_val_if_fail (LRG_IS_HDR_IMAGE (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	bytes = lrg_hdr_image_save_to_bytes (self);
	data = g_bytes_get_data (bytes, &len);
	if (!g_file_set_contents (path, (const gchar *)data, (gssize)len, &io_error))
	{
		g_set_error (error, LRG_HDR_IMAGE_ERROR, LRG_HDR_IMAGE_ERROR_IO,
		             "Cannot write %s: %s", path, io_error->message);
		g_error_free (io_error);
		return FALSE;
	}
	return TRUE;
}
