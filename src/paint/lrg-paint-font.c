/* lrg-paint-font.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * SDF font atlases, font fallback chains and bidirectional text.
 *
 * Pipeline, per text call:
 *
 *  1. Lines are split at '\n' and each is put in visual order
 *     (fribidi: UAX #9 levels, mirroring, Arabic joining/shaping).
 *  2. Each codepoint is resolved through the fallback chain and placed
 *     with unrounded `hmtx` advances plus GPOS/kern pair kerning. This
 *     step needs only the font tables, so measuring is exact and works
 *     headless.
 *  3. Glyphs not yet in the shared atlas are rasterised with raylib's
 *     LoadFontData (FONT_SDF, i.e. stb_truetype's exact distance
 *     field), whose ±2 px band is widened to LRG_PAINT_FONT_SDF_SPREAD
 *     by dead-reckoning distance propagation so outlines and glows have
 *     room, then shelf-packed into an atlas page.
 *  4. Each glyph becomes one LRG_PAINT_PRIM_GLYPH instance; the paint
 *     shader turns the sampled distance into coverage with a
 *     screen-space-derivative smoothstep, plus outline and glow.
 *
 * The TrueType table reader below covers exactly what layout needs:
 * head, hhea, hmtx, maxp, cmap (formats 0/4/6/12/13, the subtable
 * stb_truetype picks), GPOS pair kerning and the legacy kern table.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_PAINT

#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <raylib.h>
#include <rlgl.h>

#ifdef LRG_HAS_FRIBIDI
#include <fribidi.h>
#endif

#include "lrg-paint-font.h"
#include "lrg-paint-private.h"
#include "../lrg-log.h"

/* Line advance as a multiple of the font size; the same factor the
 * bitmap path in lrg-paint-list.c uses */
#define LINE_HEIGHT_FACTOR (1.2f)

/* Atlas pages start at this size and double up to the maximum */
#define ATLAS_START_SIZE (512)
#define ATLAS_MAX_SIZE   (2048)

/* Empty pixels kept between glyphs so bilinear taps never reach a
 * neighbour */
#define ATLAS_GUTTER (1)

/* raylib's FONT_SDF constants (rtext.c): padding around each glyph,
 * the on-edge value and the value change per pixel */
#define RAYLIB_SDF_PAD      (4)
#define RAYLIB_SDF_ON_EDGE  (128)
#define RAYLIB_SDF_PER_PX   (64.0f)

/* Extra padding added around raylib's glyph image so the widened field
 * fits */
#define EXTRA_PAD (LRG_PAINT_FONT_SDF_SPREAD - RAYLIB_SDF_PAD)

/* Advance of a codepoint no font has, as a fraction of the em */
#define TOFU_ADVANCE (0.6f)

/* Big-endian table tags */
#define TAG(a, b, c, d) ((guint32) (((guint32) (a) << 24) | ((guint32) (b) << 16) | \
                                    ((guint32) (c) << 8) | (guint32) (d)))

/* GLFW is linked into libregnum through raylib's desktop platform; the
 * current context identifies the GL context atlas textures belong to */
typedef struct GLFWwindow GLFWwindow;
GLFWwindow * glfwGetCurrentContext (void);

/* ==========================================================================
 * LrgPaintTextStyle
 * ========================================================================== */

G_DEFINE_BOXED_TYPE (LrgPaintTextStyle, lrg_paint_text_style,
                     lrg_paint_text_style_copy, lrg_paint_text_style_free)

void
lrg_paint_text_style_init (LrgPaintTextStyle *self,
                           const GrlColor    *color)
{
	static const GrlColor white = { 255, 255, 255, 255 };

	g_return_if_fail (self != NULL);

	memset (self, 0, sizeof (LrgPaintTextStyle));
	self->color = color != NULL ? *color : white;
	self->direction = LRG_TEXT_DIRECTION_AUTO;
}

LrgPaintTextStyle *
lrg_paint_text_style_copy (const LrgPaintTextStyle *self)
{
	g_return_val_if_fail (self != NULL, NULL);

	return g_memdup2 (self, sizeof (LrgPaintTextStyle));
}

void
lrg_paint_text_style_free (LrgPaintTextStyle *self)
{
	g_free (self);
}

/* ==========================================================================
 * TrueType tables
 * ========================================================================== */

/* One parsed face: the sfnt blob and the offsets layout needs */
typedef struct
{
	GMappedFile *mapped;        /* file source, or NULL */
	GBytes      *bytes;         /* memory source, or NULL */
	guint8      *owned;         /* face extracted from a collection */
	const guint8 *data;
	gsize        len;

	guint16      upem;
	gint16       ascent;
	gint16       descent;       /* negative, as in hhea */
	gint16       line_gap;
	guint16      n_hmetrics;
	guint16      n_glyphs;
	guint32      hmtx;
	guint32      cmap_sub;      /* absolute offset of the chosen subtable */
	guint16      cmap_format;
	guint32      kern;          /* legacy kern table, 0 if none */
	GArray      *pair_subs;     /* PairSub: GPOS pair positioning */
	GHashTable  *kern_cache;    /* (g1 << 16 | g2) -> kerning in units */
} Face;

/* A GPOS pair positioning subtable and the lookup it belongs to */
typedef struct
{
	guint32 offset;
	guint   lookup;
} PairSub;

static guint8
be8 (const Face *f,
     gsize       off)
{
	return off < f->len ? f->data[off] : 0;
}

static guint16
be16 (const Face *f,
      gsize       off)
{
	if (off + 2 > f->len)
		return 0;
	return (guint16) ((f->data[off] << 8) | f->data[off + 1]);
}

static guint32
be32 (const Face *f,
      gsize       off)
{
	if (off + 4 > f->len)
		return 0;
	return ((guint32) f->data[off] << 24) | ((guint32) f->data[off + 1] << 16) |
	       ((guint32) f->data[off + 2] << 8) | (guint32) f->data[off + 3];
}

/* find_table: offset and length of table @tag in the sfnt directory */
static gboolean
find_table (const Face *f,
            guint32     tag,
            guint32    *offset,
            guint32    *length)
{
	guint16 n;
	guint   i;

	n = be16 (f, 4);
	for (i = 0; i < n; i++)
	{
		gsize rec = 12 + 16 * (gsize) i;

		if (be32 (f, rec) == tag)
		{
			guint32 off = be32 (f, rec + 8);
			guint32 len = be32 (f, rec + 12);

			if ((gsize) off + len > f->len)
				return FALSE;
			if (offset != NULL)
				*offset = off;
			if (length != NULL)
				*length = len;
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * extract_collection_face:
 *
 * stb_truetype (inside raylib's LoadFontData) always reads the font at
 * offset 0 of the buffer it is given, which for a TrueType collection
 * is the 'ttcf' header. Rebuild face @index as a standalone sfnt: its
 * table directory with offsets rewritten, followed by copies of its
 * tables (4-byte aligned).
 */
static guint8 *
extract_collection_face (const guint8 *data,
                         gsize         len,
                         gint          index,
                         gsize        *out_len,
                         GError      **error)
{
	Face     src;
	guint32  n_faces;
	guint32  face;
	guint16  n_tables;
	gsize    total;
	gsize    pos;
	guint8  *out;
	guint    i;

	memset (&src, 0, sizeof (Face));
	src.data = data;
	src.len = len;

	n_faces = be32 (&src, 8);
	if (index < 0 || (guint32) index >= n_faces)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "font collection has %u faces, face %d requested", n_faces, index);
		return NULL;
	}
	face = be32 (&src, 12 + 4 * (gsize) index);
	n_tables = be16 (&src, (gsize) face + 4);
	if (n_tables == 0 || (gsize) face + 12 + 16 * (gsize) n_tables > len)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "font collection face has a broken table directory");
		return NULL;
	}

	/* Size: header, directory, then each table padded to 4 bytes */
	total = 12 + 16 * (gsize) n_tables;
	for (i = 0; i < n_tables; i++)
	{
		gsize   rec = (gsize) face + 12 + 16 * (gsize) i;
		guint32 off = be32 (&src, rec + 8);
		guint32 tlen = be32 (&src, rec + 12);

		if ((gsize) off + tlen > len)
		{
			g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
			                     "font collection table lies outside the file");
			return NULL;
		}
		total += ((gsize) tlen + 3) & ~(gsize) 3;
	}

	out = g_malloc0 (total);
	memcpy (out, data + face, 12);
	pos = 12 + 16 * (gsize) n_tables;
	for (i = 0; i < n_tables; i++)
	{
		gsize   rec = (gsize) face + 12 + 16 * (gsize) i;
		guint8 *dst = out + 12 + 16 * (gsize) i;
		guint32 off = be32 (&src, rec + 8);
		guint32 tlen = be32 (&src, rec + 12);

		/* tag and checksum unchanged, new offset, same length */
		memcpy (dst, data + rec, 8);
		dst[8] = (guint8) (pos >> 24);
		dst[9] = (guint8) (pos >> 16);
		dst[10] = (guint8) (pos >> 8);
		dst[11] = (guint8) pos;
		memcpy (dst + 12, data + rec + 12, 4);
		memcpy (out + pos, data + off, tlen);
		pos += ((gsize) tlen + 3) & ~(gsize) 3;
	}

	*out_len = total;
	return out;
}

/*
 * select_cmap:
 *
 * Chooses the same cmap subtable stb_truetype does (the last Unicode
 * one: platform 0, or platform 3 encoding 1 or 10), so the glyph index
 * this file computes is the one raylib rasterises.
 */
static void
select_cmap (Face    *f,
             guint32  cmap)
{
	guint16 n;
	guint   i;

	n = be16 (f, (gsize) cmap + 2);
	for (i = 0; i < n; i++)
	{
		gsize   rec = (gsize) cmap + 4 + 8 * (gsize) i;
		guint16 platform = be16 (f, rec);
		guint16 encoding = be16 (f, rec + 2);
		guint32 off = be32 (f, rec + 4);

		if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))
		{
			f->cmap_sub = cmap + off;
			f->cmap_format = be16 (f, f->cmap_sub);
		}
	}
}

/* face_glyph_index: glyph index of @cp, 0 when the face lacks it */
static guint
face_glyph_index (const Face *f,
                  gunichar    cp)
{
	gsize sub = f->cmap_sub;
	guint g = 0;

	if (sub == 0)
		return 0;

	switch (f->cmap_format)
	{
	case 0:
		if (cp < 256)
			g = be8 (f, sub + 6 + cp);
		break;

	case 4:
		{
			/* Segments sorted by end code: binary search the first
			 * segment whose end is >= cp */
			guint   seg_x2 = be16 (f, sub + 6);
			guint   segs = seg_x2 / 2;
			gsize   ends = sub + 14;
			gsize   starts = ends + seg_x2 + 2;
			gsize   deltas = starts + seg_x2;
			gsize   ranges = deltas + seg_x2;
			guint   lo = 0;
			guint   hi = segs;
			guint16 start;
			guint16 delta;
			guint16 ro;

			if (cp > 0xFFFF || segs == 0)
				break;
			while (lo < hi)
			{
				guint mid = (lo + hi) / 2;

				if (be16 (f, ends + 2 * (gsize) mid) < cp)
					lo = mid + 1;
				else
					hi = mid;
			}
			if (lo >= segs)
				break;
			start = be16 (f, starts + 2 * (gsize) lo);
			if (start > cp)
				break;
			delta = be16 (f, deltas + 2 * (gsize) lo);
			ro = be16 (f, ranges + 2 * (gsize) lo);
			if (ro == 0)
				g = (cp + delta) & 0xFFFF;
			else
			{
				g = be16 (f, ranges + 2 * (gsize) lo + ro + 2 * (gsize) (cp - start));
				if (g != 0)
					g = (g + delta) & 0xFFFF;
			}
		}
		break;

	case 6:
		{
			guint first = be16 (f, sub + 6);
			guint count = be16 (f, sub + 8);

			if (cp >= first && cp < first + count)
				g = be16 (f, sub + 10 + 2 * (gsize) (cp - first));
		}
		break;

	case 12:
	case 13:
		{
			guint32 n = be32 (f, sub + 12);
			guint32 lo = 0;
			guint32 hi = n;

			while (lo < hi)
			{
				guint32 mid = lo + (hi - lo) / 2;
				gsize   grp = sub + 16 + 12 * (gsize) mid;
				guint32 s = be32 (f, grp);
				guint32 e = be32 (f, grp + 4);

				if (cp < s)
					hi = mid;
				else if (cp > e)
					lo = mid + 1;
				else
				{
					guint32 sg = be32 (f, grp + 8);

					g = f->cmap_format == 12 ? sg + (cp - s) : sg;
					break;
				}
			}
		}
		break;

	default:
		break;
	}

	return g < f->n_glyphs ? g : 0;
}

/* face_advance: horizontal advance of glyph @g in design units */
static guint
face_advance (const Face *f,
              guint       g)
{
	guint i;

	if (f->n_hmetrics == 0)
		return 0;
	i = MIN (g, (guint) f->n_hmetrics - 1);
	return be16 (f, (gsize) f->hmtx + 4 * (gsize) i);
}

/* value_size: bytes of a GPOS ValueRecord with format @vf */
static guint
value_size (guint16 vf)
{
	guint n = 0;
	guint b;

	for (b = 0; b < 8; b++)
		if (vf & (1u << b))
			n++;
	return n * 2;
}

/* x_advance_offset: offset of XAdvance inside a ValueRecord, -1 if absent */
static gint
x_advance_offset (guint16 vf)
{
	if (!(vf & 0x0004))
		return -1;
	return ((vf & 0x0001) ? 2 : 0) + ((vf & 0x0002) ? 2 : 0);
}

/* coverage_index: index of @g in a Coverage table, -1 when not covered */
static gint
coverage_index (const Face *f,
                gsize       cov,
                guint       g)
{
	guint16 fmt = be16 (f, cov);
	guint   n = be16 (f, cov + 2);
	guint   lo = 0;
	guint   hi = n;

	while (lo < hi)
	{
		guint mid = (lo + hi) / 2;

		if (fmt == 1)
		{
			guint v = be16 (f, cov + 4 + 2 * (gsize) mid);

			if (g < v)
				hi = mid;
			else if (g > v)
				lo = mid + 1;
			else
				return (gint) mid;
		}
		else if (fmt == 2)
		{
			gsize rec = cov + 4 + 6 * (gsize) mid;
			guint s = be16 (f, rec);
			guint e = be16 (f, rec + 2);

			if (g < s)
				hi = mid;
			else if (g > e)
				lo = mid + 1;
			else
				return (gint) (be16 (f, rec + 4) + (g - s));
		}
		else
			return -1;
	}
	return -1;
}

/* class_of: class of @g in a ClassDef table (0 when unlisted) */
static guint
class_of (const Face *f,
          gsize       cd,
          guint       g)
{
	guint16 fmt = be16 (f, cd);

	if (fmt == 1)
	{
		guint start = be16 (f, cd + 2);
		guint count = be16 (f, cd + 4);

		if (g >= start && g < start + count)
			return be16 (f, cd + 6 + 2 * (gsize) (g - start));
	}
	else if (fmt == 2)
	{
		guint n = be16 (f, cd + 2);
		guint lo = 0;
		guint hi = n;

		while (lo < hi)
		{
			guint mid = (lo + hi) / 2;
			gsize rec = cd + 4 + 6 * (gsize) mid;
			guint s = be16 (f, rec);
			guint e = be16 (f, rec + 2);

			if (g < s)
				hi = mid;
			else if (g > e)
				lo = mid + 1;
			else
				return be16 (f, rec + 4);
		}
	}
	return 0;
}

/*
 * pair_adjust:
 *
 * Applies one PairPos subtable to (@g1, @g2). Returns TRUE when the
 * subtable applies (the first glyph is covered and the pair or class
 * pair exists), with the first glyph's XAdvance in @value.
 */
static gboolean
pair_adjust (const Face *f,
             gsize       sub,
             guint       g1,
             guint       g2,
             gint       *value)
{
	guint16 fmt = be16 (f, sub);
	guint16 vf1 = be16 (f, sub + 4);
	guint16 vf2 = be16 (f, sub + 6);
	gint    ci;
	gint    xo = x_advance_offset (vf1);
	guint   s1 = value_size (vf1);
	guint   s2 = value_size (vf2);

	ci = coverage_index (f, sub + be16 (f, sub + 2), g1);
	if (ci < 0)
		return FALSE;

	if (fmt == 1)
	{
		/* PairSets: second glyphs sorted, binary search */
		guint n_sets = be16 (f, sub + 8);
		gsize set;
		guint n;
		guint rs = 2 + s1 + s2;
		guint lo = 0;
		guint hi;

		if ((guint) ci >= n_sets)
			return FALSE;
		set = sub + be16 (f, sub + 10 + 2 * (gsize) ci);
		n = be16 (f, set);
		hi = n;
		while (lo < hi)
		{
			guint mid = (lo + hi) / 2;
			gsize rec = set + 2 + (gsize) rs * mid;
			guint sg = be16 (f, rec);

			if (g2 < sg)
				hi = mid;
			else if (g2 > sg)
				lo = mid + 1;
			else
			{
				*value = xo >= 0 ? (gint16) be16 (f, rec + 2 + (gsize) xo) : 0;
				return TRUE;
			}
		}
		return FALSE;
	}
	else if (fmt == 2)
	{
		/* Class pairs: a class1 x class2 matrix of value records */
		gsize cd1 = sub + be16 (f, sub + 8);
		gsize cd2 = sub + be16 (f, sub + 10);
		guint n1 = be16 (f, sub + 12);
		guint n2 = be16 (f, sub + 14);
		guint c1 = class_of (f, cd1, g1);
		guint c2 = class_of (f, cd2, g2);
		gsize rec;

		if (c1 >= n1 || c2 >= n2)
			return FALSE;
		rec = sub + 16 + ((gsize) c1 * n2 + c2) * (s1 + s2);
		*value = xo >= 0 ? (gint16) be16 (f, rec + (gsize) xo) : 0;
		return TRUE;
	}
	return FALSE;
}

/*
 * collect_gpos_kerning:
 *
 * Finds every lookup referenced by a 'kern' feature (any script) and
 * records its pair positioning subtables, unwrapping extension lookups
 * (type 9), in lookup order.
 */
static void
collect_gpos_kerning (Face    *f,
                      guint32  gpos)
{
	gsize    features = (gsize) gpos + be16 (f, (gsize) gpos + 6);
	gsize    lookups = (gsize) gpos + be16 (f, (gsize) gpos + 8);
	guint    n_features = be16 (f, features);
	guint    n_lookups = be16 (f, lookups);
	gboolean *wanted;
	guint    i;

	if (be16 (f, gpos) != 1 || n_lookups == 0)
		return;

	wanted = g_new0 (gboolean, n_lookups);
	for (i = 0; i < n_features; i++)
	{
		gsize rec = features + 2 + 6 * (gsize) i;
		gsize feat;
		guint n;
		guint j;

		if (be32 (f, rec) != TAG ('k', 'e', 'r', 'n'))
			continue;
		feat = features + be16 (f, rec + 4);
		n = be16 (f, feat + 2);
		for (j = 0; j < n; j++)
		{
			guint li = be16 (f, feat + 4 + 2 * (gsize) j);

			if (li < n_lookups)
				wanted[li] = TRUE;
		}
	}

	for (i = 0; i < n_lookups; i++)
	{
		gsize lookup;
		guint type;
		guint n_subs;
		guint k;

		if (!wanted[i])
			continue;
		lookup = lookups + be16 (f, lookups + 2 + 2 * (gsize) i);
		type = be16 (f, lookup);
		n_subs = be16 (f, lookup + 4);

		for (k = 0; k < n_subs; k++)
		{
			gsize   sub = lookup + be16 (f, lookup + 6 + 2 * (gsize) k);
			guint   sub_type = type;
			PairSub ps;

			if (type == 9 && be16 (f, sub) == 1)
			{
				sub_type = be16 (f, sub + 2);
				sub = sub + be32 (f, sub + 4);
			}
			if (sub_type != 2)
				continue;
			ps.offset = (guint32) sub;
			ps.lookup = i;
			g_array_append_val (f->pair_subs, ps);
		}
	}
	g_free (wanted);
}

/* legacy_kern: pair value from a version 0 'kern' table */
static gint
legacy_kern (const Face *f,
             guint       g1,
             guint       g2)
{
	gsize   off = f->kern;
	guint   n;
	guint   i;
	guint32 key = (g1 << 16) | g2;
	gint    total = 0;

	if (be16 (f, off) != 0)
		return 0;
	n = be16 (f, off + 2);
	off += 4;
	for (i = 0; i < n; i++)
	{
		guint sub_len = be16 (f, off + 2);
		guint coverage = be16 (f, off + 4);

		/* format 0, horizontal, not minimum values, not cross-stream */
		if ((coverage >> 8) == 0 && (coverage & 0x1) && !(coverage & 0x6))
		{
			guint np = be16 (f, off + 6);
			guint lo = 0;
			guint hi = np;

			while (lo < hi)
			{
				guint   mid = (lo + hi) / 2;
				gsize   rec = off + 14 + 6 * (gsize) mid;
				guint32 k = be32 (f, rec);

				if (key < k)
					hi = mid;
				else if (key > k)
					lo = mid + 1;
				else
				{
					total += (gint16) be16 (f, rec + 4);
					break;
				}
			}
		}
		if (sub_len == 0)
			break;
		off += sub_len;
	}
	return total;
}

/* face_kerning: pair kerning in design units, cached */
static gint
face_kerning (Face  *f,
              guint  g1,
              guint  g2)
{
	gpointer key = GUINT_TO_POINTER ((g1 << 16) | (g2 & 0xFFFF));
	gpointer cached;
	gint     total = 0;

	if (f->pair_subs->len == 0 && f->kern == 0)
		return 0;
	if (g_hash_table_lookup_extended (f->kern_cache, key, NULL, &cached))
		return GPOINTER_TO_INT (cached);

	if (f->pair_subs->len > 0)
	{
		/* Within a lookup the first applicable subtable wins; the
		 * results of separate lookups add up */
		guint    current = G_MAXUINT;
		gboolean applied = FALSE;
		guint    i;

		for (i = 0; i < f->pair_subs->len; i++)
		{
			const PairSub *ps = &g_array_index (f->pair_subs, PairSub, i);
			gint           v = 0;

			if (ps->lookup != current)
			{
				current = ps->lookup;
				applied = FALSE;
			}
			if (applied)
				continue;
			if (pair_adjust (f, ps->offset, g1, g2, &v))
			{
				total += v;
				applied = TRUE;
			}
		}
	}
	else
		total = legacy_kern (f, g1, g2);

	g_hash_table_insert (f->kern_cache, key, GINT_TO_POINTER (total));
	return total;
}

static void
face_clear (Face *f)
{
	g_clear_pointer (&f->mapped, g_mapped_file_unref);
	g_clear_pointer (&f->bytes, g_bytes_unref);
	g_clear_pointer (&f->owned, g_free);
	g_clear_pointer (&f->pair_subs, g_array_unref);
	g_clear_pointer (&f->kern_cache, g_hash_table_unref);
	f->data = NULL;
	f->len = 0;
}

/*
 * face_parse:
 *
 * Validates the sfnt in f->data (extracting face @index from a
 * collection first) and reads the metrics and table offsets.
 */
static gboolean
face_parse (Face    *f,
            gint     index,
            GError **error)
{
	guint32 head;
	guint32 hhea;
	guint32 maxp;
	guint32 cmap;
	guint32 gpos;
	guint32 version;

	if (f->len < 12)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "file is too small to be a font");
		return FALSE;
	}

	/* Collections: rebuild the face as a standalone font */
	if (be32 (f, 0) == TAG ('t', 't', 'c', 'f'))
	{
		gsize len = 0;

		f->owned = extract_collection_face (f->data, f->len, index, &len, error);
		if (f->owned == NULL)
			return FALSE;
		f->data = f->owned;
		f->len = len;
	}
	else if (index != 0)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
		             "face %d requested from a single-face font", index);
		return FALSE;
	}

	version = be32 (f, 0);
	if (version != 0x00010000 && version != TAG ('t', 'r', 'u', 'e') &&
	    version != TAG ('O', 'T', 'T', 'O'))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "not a TrueType or OpenType font");
		return FALSE;
	}

	if (!find_table (f, TAG ('h', 'e', 'a', 'd'), &head, NULL) ||
	    !find_table (f, TAG ('h', 'h', 'e', 'a'), &hhea, NULL) ||
	    !find_table (f, TAG ('h', 'm', 't', 'x'), &f->hmtx, NULL) ||
	    !find_table (f, TAG ('m', 'a', 'x', 'p'), &maxp, NULL) ||
	    !find_table (f, TAG ('c', 'm', 'a', 'p'), &cmap, NULL))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
		                     "font lacks a required table (head, hhea, hmtx, maxp, cmap)");
		return FALSE;
	}

	/* stb_truetype draws TrueType (glyf) and CFF outlines, not CFF2
	 * (variable OpenType CJK) or bitmap-only colour fonts */
	if (!find_table (f, TAG ('g', 'l', 'y', 'f'), NULL, NULL) &&
	    !find_table (f, TAG ('C', 'F', 'F', ' '), NULL, NULL))
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "font has no glyf or CFF outlines (CFF2 and bitmap fonts are not supported)");
		return FALSE;
	}

	f->upem = be16 (f, (gsize) head + 18);
	f->ascent = (gint16) be16 (f, (gsize) hhea + 4);
	f->descent = (gint16) be16 (f, (gsize) hhea + 6);
	f->line_gap = (gint16) be16 (f, (gsize) hhea + 8);
	f->n_hmetrics = be16 (f, (gsize) hhea + 34);
	f->n_glyphs = be16 (f, (gsize) maxp + 4);
	if (f->upem == 0)
		f->upem = 1000;
	if (f->ascent - f->descent <= 0)
	{
		f->ascent = (gint16) ((gint) f->upem * 8 / 10);
		f->descent = (gint16) -((gint) f->upem * 2 / 10);
	}

	select_cmap (f, cmap);
	if (f->cmap_sub == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		                     "font has no Unicode character map");
		return FALSE;
	}

	/* Kerning: GPOS pair positioning, else the legacy table */
	f->pair_subs = g_array_new (FALSE, FALSE, sizeof (PairSub));
	f->kern_cache = g_hash_table_new (g_direct_hash, g_direct_equal);
	if (find_table (f, TAG ('G', 'P', 'O', 'S'), &gpos, NULL))
		collect_gpos_kerning (f, gpos);
	if (f->pair_subs->len == 0)
		find_table (f, TAG ('k', 'e', 'r', 'n'), &f->kern, NULL);

	return TRUE;
}

/* ==========================================================================
 * Shared glyph atlas
 * ========================================================================== */

/* A horizontal strip of an atlas page that glyphs fill left to right */
typedef struct
{
	gint y;
	gint h;
	gint x;
} Shelf;

typedef struct
{
	gint      w;
	gint      h;
	guint8   *data;          /* single channel, w * h */
	GArray   *shelves;       /* Shelf */
	gint      next_y;        /* top of the free area under the shelves */

	guint     tex;           /* GL texture, 0 when not uploaded */
	gint      tex_w;
	gint      tex_h;
	gpointer  ctx;           /* GL context @tex belongs to */
	gint      dirty_y0;      /* rows changed since the upload */
	gint      dirty_y1;
} AtlasPage;

/* A texture replaced by a grown page. Instances recorded earlier in the
 * frame may still reference it, so it is kept (bounded: a page grows at
 * most twice) */
typedef struct
{
	guint    tex;
	gpointer ctx;
} RetiredTexture;

static GPtrArray *atlas_pages = NULL;
static GArray    *atlas_retired = NULL;
static guint      atlas_glyph_count = 0;

static AtlasPage *
atlas_page_new (void)
{
	AtlasPage *page = g_new0 (AtlasPage, 1);

	page->w = ATLAS_START_SIZE;
	page->h = ATLAS_START_SIZE;
	page->data = g_malloc0 ((gsize) page->w * (gsize) page->h);
	page->shelves = g_array_new (FALSE, FALSE, sizeof (Shelf));
	page->dirty_y0 = page->h;
	page->dirty_y1 = 0;
	return page;
}

/* page_try_insert: room for a @w x @h glyph in @page without growing */
static gboolean
page_try_insert (AtlasPage *page,
                 gint       w,
                 gint       h,
                 gint      *x,
                 gint      *y)
{
	gint  gw = w + ATLAS_GUTTER;
	gint  gh = h + ATLAS_GUTTER;
	gint  best = -1;
	guint i;

	/* The tightest existing shelf the glyph fits; shelves much taller
	 * than the glyph are skipped to limit waste */
	for (i = 0; i < page->shelves->len; i++)
	{
		Shelf *s = &g_array_index (page->shelves, Shelf, i);

		if (s->h >= gh && s->h <= gh + gh / 2 + 4 && s->x + gw <= page->w)
			if (best < 0 || s->h < g_array_index (page->shelves, Shelf, best).h)
				best = (gint) i;
	}
	if (best >= 0)
	{
		Shelf *s = &g_array_index (page->shelves, Shelf, best);

		*x = s->x;
		*y = s->y;
		s->x += gw;
		return TRUE;
	}

	/* A new shelf under the existing ones */
	if (page->next_y + gh <= page->h && gw <= page->w)
	{
		Shelf s;

		s.y = page->next_y;
		s.h = gh;
		s.x = gw;
		g_array_append_val (page->shelves, s);
		page->next_y += gh;
		*x = 0;
		*y = s.y;
		return TRUE;
	}
	return FALSE;
}

/*
 * page_grow:
 *
 * Doubles both sides of a page. Existing glyphs keep their pixel
 * positions (the instances address the atlas in pixels, the shader
 * divides by the bound texture's size), shelves become wider and the
 * texture is re-created on the next upload.
 */
static void
page_grow (AtlasPage *page)
{
	gint    nw = page->w * 2;
	gint    nh = page->h * 2;
	guint8 *data;
	gint    row;

	data = g_malloc0 ((gsize) nw * (gsize) nh);
	for (row = 0; row < page->h; row++)
		memcpy (data + (gsize) row * (gsize) nw, page->data + (gsize) row * (gsize) page->w,
		        (gsize) page->w);
	g_free (page->data);
	page->data = data;
	page->w = nw;
	page->h = nh;
	page->dirty_y0 = 0;
	page->dirty_y1 = nh;
}

/* atlas_insert: find room for a glyph, growing or adding pages */
static void
atlas_insert (gint   w,
              gint   h,
              guint *page_index,
              gint  *x,
              gint  *y)
{
	AtlasPage *page;
	guint      i;

	if (atlas_pages == NULL)
		atlas_pages = g_ptr_array_new ();

	for (i = 0; i < atlas_pages->len; i++)
	{
		if (page_try_insert (g_ptr_array_index (atlas_pages, i), w, h, x, y))
		{
			*page_index = i;
			return;
		}
	}

	/* Grow the last page while it may */
	if (atlas_pages->len > 0)
	{
		page = g_ptr_array_index (atlas_pages, atlas_pages->len - 1);
		while (page->w < ATLAS_MAX_SIZE)
		{
			page_grow (page);
			lrg_debug (LRG_LOG_DOMAIN_PAINT, "glyph atlas page %u grown to %dx%d",
			           atlas_pages->len - 1, page->w, page->h);
			if (page_try_insert (page, w, h, x, y))
			{
				*page_index = atlas_pages->len - 1;
				return;
			}
		}
	}

	/* A new page; a glyph always fits an empty one */
	page = atlas_page_new ();
	g_ptr_array_add (atlas_pages, page);
	*page_index = atlas_pages->len - 1;
	if (!page_try_insert (page, w, h, x, y))
	{
		*x = 0;
		*y = 0;
	}
	lrg_debug (LRG_LOG_DOMAIN_PAINT, "glyph atlas page %u added", *page_index);
}

/* atlas_write: copy a glyph's field into the page and mark it dirty */
static void
atlas_write (guint         page_index,
             gint          x,
             gint          y,
             gint          w,
             gint          h,
             const guint8 *src)
{
	AtlasPage *page = g_ptr_array_index (atlas_pages, page_index);
	gint       row;

	for (row = 0; row < h; row++)
	{
		if (y + row >= page->h)
			break;
		memcpy (page->data + (gsize) (y + row) * (gsize) page->w + (gsize) x,
		        src + (gsize) row * (gsize) w, (gsize) MIN (w, page->w - x));
	}
	page->dirty_y0 = MIN (page->dirty_y0, y);
	page->dirty_y1 = MAX (page->dirty_y1, MIN (y + h, page->h));
}

/*
 * atlas_page_texture:
 *
 * The GL texture of a page, created or updated as needed. Uploads only
 * the dirty rows (full width, so the rows are contiguous). Returns 0
 * without a window. A texture from an earlier GL context (window closed
 * and reopened) is forgotten, not deleted: that context is gone.
 */
static guint
atlas_page_texture (guint page_index)
{
	AtlasPage *page;
	gpointer   ctx;

	if (atlas_pages == NULL || page_index >= atlas_pages->len || !IsWindowReady ())
		return 0;

	page = g_ptr_array_index (atlas_pages, page_index);
	ctx = glfwGetCurrentContext ();

	if (page->tex != 0 && page->ctx != ctx)
		page->tex = 0;

	if (page->tex != 0 && (page->tex_w != page->w || page->tex_h != page->h))
	{
		RetiredTexture r;

		if (atlas_retired == NULL)
			atlas_retired = g_array_new (FALSE, FALSE, sizeof (RetiredTexture));
		r.tex = page->tex;
		r.ctx = page->ctx;
		g_array_append_val (atlas_retired, r);
		page->tex = 0;
	}

	if (page->tex == 0)
	{
		page->tex = rlLoadTexture (page->data, page->w, page->h,
		                           RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
		if (page->tex == 0)
			return 0;
		rlTextureParameters (page->tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
		rlTextureParameters (page->tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
		rlTextureParameters (page->tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
		rlTextureParameters (page->tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
		page->tex_w = page->w;
		page->tex_h = page->h;
		page->ctx = ctx;
		page->dirty_y0 = page->h;
		page->dirty_y1 = 0;
		return page->tex;
	}

	if (page->dirty_y1 > page->dirty_y0)
	{
		rlUpdateTexture (page->tex, 0, page->dirty_y0, page->w,
		                 page->dirty_y1 - page->dirty_y0,
		                 RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE,
		                 page->data + (gsize) page->dirty_y0 * (gsize) page->w);
		page->dirty_y0 = page->h;
		page->dirty_y1 = 0;
	}
	return page->tex;
}

guint
lrg_paint_font_atlas_get_n_pages (void)
{
	return atlas_pages != NULL ? atlas_pages->len : 0;
}

gboolean
lrg_paint_font_atlas_get_page_size (guint  page,
                                    gint  *width,
                                    gint  *height)
{
	AtlasPage *p;

	if (atlas_pages == NULL || page >= atlas_pages->len)
		return FALSE;
	p = g_ptr_array_index (atlas_pages, page);
	if (width != NULL)
		*width = p->w;
	if (height != NULL)
		*height = p->h;
	return TRUE;
}

guint
lrg_paint_font_atlas_get_n_glyphs (void)
{
	return atlas_glyph_count;
}

/* ==========================================================================
 * Distance field widening
 * ========================================================================== */

/*
 * widen_field:
 *
 * raylib's SDF glyphs (stb_truetype) hold the exact distance to the
 * outline, but only within ±2 px (value = 128 + 64·d, clamped). That is
 * enough for anti-aliasing, not for outlines and glows. This grows the
 * image by EXTRA_PAD on each side and extends the field outwards and
 * inwards with dead reckoning: every pixel inside the exact band is a
 * seed carrying its exact distance; two raster passes propagate, for
 * every other pixel, the seed minimising |p - seed| + d(seed). The
 * result is re-encoded so the 0..255 range spans ±SDF_SPREAD px.
 */
static void
widen_field (const guint8 *src,
             gint          w,
             gint          h,
             guint8       *dst)
{
	static const gint fwd[4][2] = { { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
	static const gint bwd[4][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 } };
	gint      e = EXTRA_PAD;
	gint      W = w + 2 * e;
	gint      H = h + 2 * e;
	gsize     n = (gsize) W * (gsize) H;
	gfloat   *dist;
	gfloat   *seed_d;
	gint     *seed;
	gboolean *inside;
	gint      x;
	gint      y;
	gint      pass;
	gsize     i;
	gfloat    spread = (gfloat) LRG_PAINT_FONT_SDF_SPREAD;

	dist = g_new (gfloat, n);
	seed_d = g_new (gfloat, n);
	seed = g_new (gint, n);
	inside = g_new0 (gboolean, n);

	/* 1. Seeds from the exact band; everything else unknown */
	for (y = 0; y < H; y++)
	{
		for (x = 0; x < W; x++)
		{
			gsize p = (gsize) y * (gsize) W + (gsize) x;
			gint  sx = x - e;
			gint  sy = y - e;

			dist[p] = G_MAXFLOAT;
			seed[p] = -1;
			seed_d[p] = 0.0f;
			if (sx >= 0 && sy >= 0 && sx < w && sy < h)
			{
				guint8 v = src[(gsize) sy * (gsize) w + (gsize) sx];

				inside[p] = v >= RAYLIB_SDF_ON_EDGE;
				if (v > 0 && v < 255)
				{
					seed_d[p] = fabsf (((gfloat) v - (gfloat) RAYLIB_SDF_ON_EDGE) / RAYLIB_SDF_PER_PX);
					dist[p] = seed_d[p];
					seed[p] = (gint) p;
				}
			}
		}
	}

	/* 2. Forward and backward raster passes, twice for accuracy */
	for (pass = 0; pass < 4; pass++)
	{
		const gint (*nb)[2] = (pass % 2 == 0) ? fwd : bwd;
		gint       y0 = (pass % 2 == 0) ? 0 : H - 1;
		gint       y1 = (pass % 2 == 0) ? H : -1;
		gint       x0 = (pass % 2 == 0) ? 0 : W - 1;
		gint       x1 = (pass % 2 == 0) ? W : -1;
		gint       step = (pass % 2 == 0) ? 1 : -1;

		for (y = y0; y != y1; y += step)
		{
			for (x = x0; x != x1; x += step)
			{
				gsize p = (gsize) y * (gsize) W + (gsize) x;
				guint k;

				for (k = 0; k < 4; k++)
				{
					gint   nx = x + nb[k][0];
					gint   ny = y + nb[k][1];
					gint   s;
					gfloat dx;
					gfloat dy;
					gfloat cand;

					if (nx < 0 || ny < 0 || nx >= W || ny >= H)
						continue;
					s = seed[(gsize) ny * (gsize) W + (gsize) nx];
					if (s < 0)
						continue;
					dx = (gfloat) (x - s % W);
					dy = (gfloat) (y - s / W);
					cand = sqrtf (dx * dx + dy * dy) + seed_d[s];
					if (cand < dist[p])
					{
						dist[p] = cand;
						seed[p] = s;
					}
				}
			}
		}
	}

	/* 3. Re-encode: 128 on the edge, ±127 at ±spread */
	for (i = 0; i < n; i++)
	{
		gfloat d = dist[i] == G_MAXFLOAT ? spread : MIN (dist[i], spread);
		gfloat v;

		if (!inside[i])
			d = -d;
		v = 128.0f + d * 127.0f / spread;
		dst[i] = (guint8) CLAMP ((gint) (v + 0.5f), 0, 255);
	}

	g_free (dist);
	g_free (seed_d);
	g_free (seed);
	g_free (inside);
}

/* ==========================================================================
 * LrgPaintFont
 * ========================================================================== */

/* One codepoint of one face: its metrics and, once rasterised, where
 * its field lives in the atlas */
typedef struct
{
	guint    gid;
	guint    advance;       /* design units */
	gboolean rasterised;
	gboolean ink;           /* FALSE for blank glyphs (spaces) */
	guint    page;
	gint     ax;
	gint     ay;
	gint     w;
	gint     h;
	gint     off_x;         /* base pixels, from raylib, widened */
	gint     off_y;
} Glyph;

struct _LrgPaintFont
{
	GObject     parent_instance;

	gchar      *path;
	gint        face_index;
	GBytes     *source;       /* for fonts made from bytes */
	gint        base_size;

	Face        face;
	gboolean    face_loaded;
	gboolean    face_failed;
	gfloat      base_scale;   /* base pixels per design unit (raylib's) */
	gint        base_ascent;  /* (int) (ascent * base_scale), as raylib */

	GHashTable *glyphs;       /* codepoint -> Glyph */
	GHashTable *resolved;     /* codepoint -> chain index + 2 */
	GPtrArray  *fallbacks;    /* LrgPaintFont */
	guint       n_rasterised;
};

G_DEFINE_FINAL_TYPE (LrgPaintFont, lrg_paint_font, G_TYPE_OBJECT)

enum
{
	PROP_0,
	PROP_PATH,
	PROP_BASE_SIZE,
	N_PROPS
};

static GParamSpec *properties[N_PROPS];

static LrgPaintFont *default_font = NULL;
static GQuark        bind_quark = 0;

static void
lrg_paint_font_finalize (GObject *object)
{
	LrgPaintFont *self = LRG_PAINT_FONT (object);

	face_clear (&self->face);
	g_clear_pointer (&self->source, g_bytes_unref);
	g_clear_pointer (&self->path, g_free);
	g_clear_pointer (&self->glyphs, g_hash_table_unref);
	g_clear_pointer (&self->resolved, g_hash_table_unref);
	g_clear_pointer (&self->fallbacks, g_ptr_array_unref);

	G_OBJECT_CLASS (lrg_paint_font_parent_class)->finalize (object);
}

static void
lrg_paint_font_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
	LrgPaintFont *self = LRG_PAINT_FONT (object);

	switch (prop_id)
	{
	case PROP_PATH:
		g_value_set_string (value, self->path);
		break;
	case PROP_BASE_SIZE:
		g_value_set_int (value, self->base_size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_paint_font_class_init (LrgPaintFontClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_paint_font_finalize;
	object_class->get_property = lrg_paint_font_get_property;

	/**
	 * LrgPaintFont:path:
	 *
	 * The file the font was opened from, %NULL for fonts made from
	 * bytes.
	 */
	properties[PROP_PATH] =
		g_param_spec_string ("path", NULL, "Font file", NULL,
		                     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * LrgPaintFont:base-size:
	 *
	 * Pixel size (ascent plus descent) the glyph distance fields are
	 * generated at.
	 */
	properties[PROP_BASE_SIZE] =
		g_param_spec_int ("base-size", NULL, "SDF generation size", 8, 512,
		                  LRG_PAINT_FONT_DEFAULT_BASE_SIZE,
		                  G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_paint_font_init (LrgPaintFont *self)
{
	self->base_size = LRG_PAINT_FONT_DEFAULT_BASE_SIZE;
	self->glyphs = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	self->resolved = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->fallbacks = g_ptr_array_new_with_free_func (g_object_unref);
}

/* finish_face: derived metrics once the face is parsed */
static void
finish_face (LrgPaintFont *self)
{
	Face *f = &self->face;

	/* raylib: stbtt_ScaleForPixelHeight = size / (ascent - descent),
	 * and glyph offsetY includes (int) (ascent * scale) */
	self->base_scale = (gfloat) self->base_size / (gfloat) (f->ascent - f->descent);
	self->base_ascent = (gint) ((gfloat) f->ascent * self->base_scale);
	self->face_loaded = TRUE;
}

/*
 * load_face:
 *
 * Maps the font file (or takes the bytes) and parses it. Lazily created
 * fallback fonts call this on first use; their errors are logged once.
 */
static gboolean
load_face (LrgPaintFont  *self,
           GError       **error)
{
	if (self->face_loaded)
		return TRUE;
	if (self->face_failed)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "font %s could not be loaded",
		             self->path != NULL ? self->path : "(memory)");
		return FALSE;
	}

	memset (&self->face, 0, sizeof (Face));
	if (self->source != NULL)
	{
		self->face.bytes = g_bytes_ref (self->source);
		self->face.data = g_bytes_get_data (self->source, &self->face.len);
	}
	else
	{
		self->face.mapped = g_mapped_file_new (self->path, FALSE, error);
		if (self->face.mapped == NULL)
		{
			self->face_failed = TRUE;
			return FALSE;
		}
		self->face.data = (const guint8 *) g_mapped_file_get_contents (self->face.mapped);
		self->face.len = g_mapped_file_get_length (self->face.mapped);
	}

	if (self->face.data == NULL || self->face.len == 0)
	{
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "font file is empty");
		face_clear (&self->face);
		self->face_failed = TRUE;
		return FALSE;
	}
	if (!face_parse (&self->face, self->face_index, error))
	{
		face_clear (&self->face);
		self->face_failed = TRUE;
		return FALSE;
	}

	finish_face (self);
	return TRUE;
}

/* ensure_face: load_face() for internal callers, logging failures once */
static gboolean
ensure_face (LrgPaintFont *self)
{
	g_autoptr(GError) error = NULL;

	if (self->face_loaded)
		return TRUE;
	if (self->face_failed)
		return FALSE;
	if (!load_face (self, &error))
	{
		lrg_warning (LRG_LOG_DOMAIN_PAINT, "font %s: %s",
		             self->path != NULL ? self->path : "(memory)", error->message);
		return FALSE;
	}
	return TRUE;
}

/* font_new_lazy: a font whose file is opened on first use */
static LrgPaintFont *
font_new_lazy (const gchar *path,
               gint         face_index,
               gint         base_size)
{
	LrgPaintFont *self = g_object_new (LRG_TYPE_PAINT_FONT, NULL);

	self->path = g_strdup (path);
	self->face_index = face_index;
	self->base_size = base_size > 0 ? CLAMP (base_size, 8, 512) : LRG_PAINT_FONT_DEFAULT_BASE_SIZE;
	return self;
}

LrgPaintFont *
lrg_paint_font_new_from_file_index (const gchar  *path,
                                    gint          face_index,
                                    gint          base_size,
                                    GError      **error)
{
	g_autoptr(LrgPaintFont) self = NULL;

	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	self = font_new_lazy (path, face_index, base_size);
	if (!load_face (self, error))
	{
		g_prefix_error (error, "%s: ", path);
		return NULL;
	}
	return g_steal_pointer (&self);
}

LrgPaintFont *
lrg_paint_font_new_from_file (const gchar  *path,
                              GError      **error)
{
	return lrg_paint_font_new_from_file_index (path, 0, 0, error);
}

LrgPaintFont *
lrg_paint_font_new_from_bytes (GBytes  *data,
                               GError **error)
{
	g_autoptr(LrgPaintFont) self = NULL;

	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	self = g_object_new (LRG_TYPE_PAINT_FONT, NULL);
	self->source = g_bytes_ref (data);
	if (!load_face (self, error))
		return NULL;
	return g_steal_pointer (&self);
}

const gchar *
lrg_paint_font_get_path (LrgPaintFont *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), NULL);

	return self->path;
}

gint
lrg_paint_font_get_base_size (LrgPaintFont *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0);

	return self->base_size;
}

guint
lrg_paint_font_get_units_per_em (LrgPaintFont *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0);

	if (!ensure_face (self))
		return 0;
	return self->face.upem;
}

/* ==========================================================================
 * Glyphs
 * ========================================================================== */

/* is_invisible: format and joining controls drawn as nothing */
static gboolean
is_invisible (gunichar cp)
{
	return cp == '\r' || cp == 0x00AD || cp == 0x034F ||
	       (cp >= 0x200B && cp <= 0x200F) ||
	       (cp >= 0x202A && cp <= 0x202E) ||
	       (cp >= 0x2060 && cp <= 0x2069) ||
	       (cp >= 0xFE00 && cp <= 0xFE0F) ||
	       cp == 0xFEFF ||
	       (cp >= 0xE0100 && cp <= 0xE01EF);
}

/* get_glyph: the cached metrics of @cp in this face (no rasterising);
 * NULL when the face lacks it */
static Glyph *
get_glyph (LrgPaintFont *self,
           gunichar      cp)
{
	Glyph *glyph;
	guint  gid;

	glyph = g_hash_table_lookup (self->glyphs, GUINT_TO_POINTER (cp));
	if (glyph != NULL)
		return glyph->gid != 0 ? glyph : NULL;
	if (!ensure_face (self))
		return NULL;

	gid = face_glyph_index (&self->face, cp);
	glyph = g_new0 (Glyph, 1);
	glyph->gid = gid;
	glyph->advance = gid != 0 ? face_advance (&self->face, gid) : 0;
	g_hash_table_insert (self->glyphs, GUINT_TO_POINTER (cp), glyph);
	return gid != 0 ? glyph : NULL;
}

/*
 * rasterise:
 *
 * Generates @glyph's distance field with raylib (stb_truetype's exact
 * SDF), widens it and packs it into the shared atlas. Blank glyphs are
 * only flagged. Returns TRUE when work was done.
 */
static gboolean
rasterise (LrgPaintFont *self,
           gunichar      cp,
           Glyph        *glyph)
{
	GlyphInfo *info;
	gint       count = 0;
	gint       codepoint = (gint) cp;
	gint       w;
	gint       h;

	if (glyph->rasterised)
		return FALSE;
	glyph->rasterised = TRUE;
	glyph->ink = FALSE;

	if (g_unichar_isspace (cp))
		return FALSE;

	info = LoadFontData (self->face.data, (gint) self->face.len, self->base_size,
	                     &codepoint, 1, FONT_SDF, &count);
	if (info == NULL || count < 1)
	{
		if (info != NULL)
			UnloadFontData (info, count);
		return FALSE;
	}

	w = info[0].image.width;
	h = info[0].image.height;
	if (info[0].image.data != NULL && w > 0 && h > 0)
	{
		const guint8 *src = info[0].image.data;
		gsize         i;

		/* Ink: any pixel on or inside the outline */
		for (i = 0; i < (gsize) w * (gsize) h; i++)
		{
			if (src[i] > RAYLIB_SDF_ON_EDGE)
			{
				glyph->ink = TRUE;
				break;
			}
		}

		if (glyph->ink)
		{
			g_autofree guint8 *wide = NULL;
			gint               ww = w + 2 * EXTRA_PAD;
			gint               wh = h + 2 * EXTRA_PAD;

			wide = g_malloc ((gsize) ww * (gsize) wh);
			widen_field (src, w, h, wide);
			atlas_insert (ww, wh, &glyph->page, &glyph->ax, &glyph->ay);
			atlas_write (glyph->page, glyph->ax, glyph->ay, ww, wh, wide);
			glyph->w = ww;
			glyph->h = wh;
			glyph->off_x = info[0].offsetX - EXTRA_PAD;
			glyph->off_y = info[0].offsetY - EXTRA_PAD;
			self->n_rasterised++;
			atlas_glyph_count++;
		}
	}

	UnloadFontData (info, count);
	return TRUE;
}

gboolean
lrg_paint_font_has_glyph (LrgPaintFont *self,
                          gunichar      codepoint)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), FALSE);

	return get_glyph (self, codepoint) != NULL;
}

guint
lrg_paint_font_get_n_glyphs (LrgPaintFont *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0);

	return self->n_rasterised;
}

/* ==========================================================================
 * Fallback chain
 * ========================================================================== */

void
lrg_paint_font_add_fallback (LrgPaintFont *self,
                             LrgPaintFont *fallback)
{
	guint i;

	g_return_if_fail (LRG_IS_PAINT_FONT (self));
	g_return_if_fail (LRG_IS_PAINT_FONT (fallback));

	if (fallback == self)
		return;
	for (i = 0; i < self->fallbacks->len; i++)
		if (g_ptr_array_index (self->fallbacks, i) == fallback)
			return;

	g_ptr_array_add (self->fallbacks, g_object_ref (fallback));
	g_hash_table_remove_all (self->resolved);
}

void
lrg_paint_font_clear_fallbacks (LrgPaintFont *self)
{
	g_return_if_fail (LRG_IS_PAINT_FONT (self));

	g_ptr_array_set_size (self->fallbacks, 0);
	g_hash_table_remove_all (self->resolved);
}

GPtrArray *
lrg_paint_font_get_fallbacks (LrgPaintFont *self)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), NULL);

	return self->fallbacks;
}

LrgPaintFont *
lrg_paint_font_resolve (LrgPaintFont *self,
                        gunichar      codepoint)
{
	gpointer cached;
	gint     index = -1;
	guint    i;

	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), NULL);

	/* Cache: chain index + 2 (1 = no font, 2 = self, 3.. fallbacks) */
	cached = g_hash_table_lookup (self->resolved, GUINT_TO_POINTER (codepoint));
	if (cached != NULL)
	{
		index = GPOINTER_TO_INT (cached) - 2;
	}
	else
	{
		if (get_glyph (self, codepoint) != NULL)
			index = 0;
		for (i = 0; index < 0 && i < self->fallbacks->len; i++)
			if (get_glyph (g_ptr_array_index (self->fallbacks, i), codepoint) != NULL)
				index = (gint) i + 1;
		g_hash_table_insert (self->resolved, GUINT_TO_POINTER (codepoint),
		                     GINT_TO_POINTER (index + 2));
	}

	if (index < 0)
		return NULL;
	if (index == 0)
		return self;
	return g_ptr_array_index (self->fallbacks, index - 1);
}

/* ==========================================================================
 * Metrics
 * ========================================================================== */

/* em_px: pixels per em at @size for the primary face; fallback faces
 * are scaled to the same em so mixed scripts have matching sizes */
static gfloat
em_px (LrgPaintFont *self,
       gfloat        size)
{
	Face *f = &self->face;

	return size * (gfloat) f->upem / (gfloat) (f->ascent - f->descent);
}

/* unit_scale: canvas units per design unit of @font for an em of @em */
static gfloat
unit_scale (LrgPaintFont *font,
            gfloat        em)
{
	return em / (gfloat) font->face.upem;
}

gfloat
lrg_paint_font_get_ascent (LrgPaintFont *self,
                           gfloat        size)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return size * 0.8f;
	return (gfloat) self->face.ascent * size / (gfloat) (self->face.ascent - self->face.descent);
}

gfloat
lrg_paint_font_get_descent (LrgPaintFont *self,
                            gfloat        size)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return size * 0.2f;
	return -(gfloat) self->face.descent * size / (gfloat) (self->face.ascent - self->face.descent);
}

gfloat
lrg_paint_font_get_line_height (LrgPaintFont *self,
                                gfloat        size)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return size * LINE_HEIGHT_FACTOR;
	return size + (gfloat) MAX (self->face.line_gap, 0) * size /
	              (gfloat) (self->face.ascent - self->face.descent);
}

gfloat
lrg_paint_font_get_advance (LrgPaintFont *self,
                            gunichar      codepoint,
                            gfloat        size)
{
	LrgPaintFont *font;
	Glyph        *glyph;

	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return 0.0f;
	font = lrg_paint_font_resolve (self, codepoint);
	if (font == NULL)
		return em_px (self, size) * TOFU_ADVANCE;
	glyph = get_glyph (font, codepoint);
	return (gfloat) glyph->advance * unit_scale (font, em_px (self, size));
}

gfloat
lrg_paint_font_get_kerning (LrgPaintFont *self,
                            gunichar      left,
                            gunichar      right,
                            gfloat        size)
{
	LrgPaintFont *font;
	Glyph        *a;
	Glyph        *b;

	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return 0.0f;
	font = lrg_paint_font_resolve (self, left);
	if (font == NULL || font != lrg_paint_font_resolve (self, right))
		return 0.0f;
	a = get_glyph (font, left);
	b = get_glyph (font, right);
	return (gfloat) face_kerning (&font->face, a->gid, b->gid) *
	       unit_scale (font, em_px (self, size));
}

gfloat
lrg_paint_font_get_max_effect (LrgPaintFont *self,
                               gfloat        size)
{
	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0.0f);

	if (!ensure_face (self))
		return 0.0f;
	/* the spread is in base pixels of the primary face */
	return (gfloat) LRG_PAINT_FONT_SDF_SPREAD * size / (gfloat) self->base_size;
}

/* ==========================================================================
 * Bidirectional text
 * ========================================================================== */

gboolean
lrg_paint_text_has_bidi (void)
{
#ifdef LRG_HAS_FRIBIDI
	return TRUE;
#else
	return FALSE;
#endif
}

/* is_rtl_char: strong right-to-left scripts and the RTL controls */
static gboolean
is_rtl_char (gunichar cp)
{
	return (cp >= 0x0590 && cp <= 0x08FF) ||
	       (cp >= 0xFB1D && cp <= 0xFDFF) ||
	       (cp >= 0xFE70 && cp <= 0xFEFE) ||
	       (cp >= 0x10800 && cp <= 0x10FFF) ||
	       (cp >= 0x1E800 && cp <= 0x1EFFF) ||
	       cp == 0x200F || cp == 0x202B || cp == 0x202E || cp == 0x2067;
}

/* first_strong: direction of the first strong character (heuristic
 * used when fribidi is absent and to skip it for plain LTR text) */
static LrgTextDirection
first_strong (const gunichar *cps,
              glong           n)
{
	glong i;

	for (i = 0; i < n; i++)
	{
		if (is_rtl_char (cps[i]))
			return LRG_TEXT_DIRECTION_RTL;
		if (g_unichar_isalpha (cps[i]))
			return LRG_TEXT_DIRECTION_LTR;
	}
	return LRG_TEXT_DIRECTION_LTR;
}

/*
 * to_visual:
 *
 * One line, logical order in, visual order out (g_free the result).
 * Pure left-to-right lines (no RTL character and no forced RTL base)
 * skip fribidi entirely.
 */
static gunichar *
to_visual (const gchar      *line,
           gssize            length,
           LrgTextDirection  direction,
           glong            *n_out,
           LrgTextDirection *resolved)
{
	gunichar        *cps;
	glong            n = 0;
	glong            i;
	gboolean         any_rtl = FALSE;
	LrgTextDirection res;

	cps = g_utf8_to_ucs4_fast (line, length, &n);
	*n_out = n;

	for (i = 0; i < n && !any_rtl; i++)
		any_rtl = is_rtl_char (cps[i]);

	if (direction == LRG_TEXT_DIRECTION_AUTO)
		res = any_rtl ? first_strong (cps, n) : LRG_TEXT_DIRECTION_LTR;
	else
		res = direction;

#ifdef LRG_HAS_FRIBIDI
	if (n > 0 && (any_rtl || direction == LRG_TEXT_DIRECTION_RTL))
	{
		FriBidiParType      pdir;
		FriBidiCharType    *types;
		FriBidiBracketType *btypes;
		FriBidiLevel       *levels;
		FriBidiArabicProp  *props;
		FriBidiChar        *str = (FriBidiChar *) cps;
		FriBidiFlags        flags = FRIBIDI_FLAGS_DEFAULT | FRIBIDI_FLAGS_ARABIC;

		G_STATIC_ASSERT (sizeof (FriBidiChar) == sizeof (gunichar));

		pdir = direction == LRG_TEXT_DIRECTION_RTL ? FRIBIDI_PAR_RTL
		     : direction == LRG_TEXT_DIRECTION_LTR ? FRIBIDI_PAR_LTR
		     : FRIBIDI_PAR_ON;
		types = g_new (FriBidiCharType, n);
		btypes = g_new (FriBidiBracketType, n);
		levels = g_new (FriBidiLevel, n);
		props = g_new (FriBidiArabicProp, n);

		/* UAX #9: types, bracket pairs, embedding levels */
		fribidi_get_bidi_types (str, (FriBidiStrIndex) n, types);
		fribidi_get_bracket_types (str, (FriBidiStrIndex) n, types, btypes);
		if (fribidi_get_par_embedding_levels_ex (types, btypes, (FriBidiStrIndex) n,
		                                         &pdir, levels) != 0)
		{
			/* Arabic joining, then mirroring and presentation forms,
			 * then reorder runs into visual order */
			fribidi_get_joining_types (str, (FriBidiStrIndex) n, props);
			fribidi_join_arabic (types, (FriBidiStrIndex) n, levels, props);
			fribidi_shape (flags, levels, (FriBidiStrIndex) n, props, str);
			if (fribidi_reorder_line (flags, types, (FriBidiStrIndex) n, 0, pdir,
			                          levels, str, NULL) != 0)
				res = FRIBIDI_IS_RTL (pdir) ? LRG_TEXT_DIRECTION_RTL : LRG_TEXT_DIRECTION_LTR;
		}

		g_free (types);
		g_free (btypes);
		g_free (levels);
		g_free (props);
	}
#endif

	if (resolved != NULL)
		*resolved = res;
	return cps;
}

gchar *
lrg_paint_text_reorder (const gchar      *text,
                        gssize            length,
                        LrgTextDirection  direction,
                        LrgTextDirection *resolved)
{
	g_autofree gunichar *cps = NULL;
	glong                n = 0;
	gchar               *out;

	g_return_val_if_fail (text != NULL, NULL);

	cps = to_visual (text, length, direction, &n, resolved);
	out = g_ucs4_to_utf8 (cps, n, NULL, NULL, NULL);
	return out != NULL ? out : g_strdup ("");
}

/* ==========================================================================
 * Layout
 * ========================================================================== */

/* One glyph placed on a line */
typedef struct
{
	LrgPaintFont *font;     /* NULL: no font has it (drawn as a box) */
	gunichar      cp;
	gfloat        x;        /* pen position from the line start */
	gfloat        y;        /* top of the line */
	gfloat        advance;
} Placed;

/*
 * layout_line:
 *
 * Places the visual-order codepoints of one line: fallback resolution,
 * unrounded advances, kerning between neighbours of the same face and
 * @spacing after each glyph. Returns the line width (the trailing
 * spacing is not ink, as in the bitmap path).
 */
static gfloat
layout_line (LrgPaintFont   *self,
             const gunichar *cps,
             glong           n,
             gfloat          size,
             gfloat          spacing,
             gfloat          top,
             GArray         *out)
{
	gfloat        em = em_px (self, size);
	gfloat        pen = 0.0f;
	LrgPaintFont *prev_font = NULL;
	guint         prev_gid = 0;
	guint         count = 0;
	glong         i;

	for (i = 0; i < n; i++)
	{
		gunichar      cp = cps[i];
		LrgPaintFont *font;
		Placed        p;

		if (is_invisible (cp) || cp == '\n')
			continue;

		p.cp = cp;
		p.y = top;

		if (cp == '\t')
		{
			/* a tab is four spaces wide */
			p.font = NULL;
			p.cp = ' ';
			p.advance = 4.0f * lrg_paint_font_get_advance (self, ' ', size);
			prev_font = NULL;
		}
		else
		{
			font = lrg_paint_font_resolve (self, cp);
			p.font = font;
			if (font == NULL)
			{
				p.advance = g_unichar_isspace (cp)
				            ? lrg_paint_font_get_advance (self, ' ', size)
				            : em * TOFU_ADVANCE;
				if (g_unichar_isspace (cp))
					p.cp = ' ';
				prev_font = NULL;
			}
			else
			{
				Glyph *glyph = get_glyph (font, cp);
				gfloat us = unit_scale (font, em);

				if (prev_font == font)
					pen += (gfloat) face_kerning (&font->face, prev_gid, glyph->gid) * us;
				p.advance = (gfloat) glyph->advance * us;
				prev_font = font;
				prev_gid = glyph->gid;
			}
		}

		p.x = pen;
		g_array_append_val (out, p);
		pen += p.advance + spacing;
		count++;
	}

	return count > 0 ? pen - spacing : 0.0f;
}

/*
 * layout_text:
 *
 * Splits @text into lines, puts each in visual order and lays it out.
 * Returns the widest line; @n_lines receives the line count.
 */
static gfloat
layout_text (LrgPaintFont     *self,
             const gchar      *text,
             gssize            length,
             gfloat            size,
             gfloat            spacing,
             LrgTextDirection  direction,
             GArray           *out,
             guint            *n_lines)
{
	const gchar *p = text;
	const gchar *end;
	gfloat       max_w = 0.0f;
	gfloat       top = 0.0f;
	guint        lines = 0;

	end = length < 0 ? text + strlen (text) : text + length;

	while (TRUE)
	{
		const gchar         *nl = memchr (p, '\n', (gsize) (end - p));
		const gchar         *line_end = nl != NULL ? nl : end;
		g_autofree gunichar *cps = NULL;
		glong                n = 0;
		gfloat               w;

		cps = to_visual (p, line_end - p, direction, &n, NULL);
		w = layout_line (self, cps, n, size, spacing, top, out);
		max_w = MAX (max_w, w);
		lines++;
		top += size * LINE_HEIGHT_FACTOR;

		if (nl == NULL)
			break;
		p = nl + 1;
	}

	if (n_lines != NULL)
		*n_lines = lines;
	return max_w;
}

void
lrg_paint_font_measure_text (LrgPaintFont     *self,
                             const gchar      *text,
                             gssize            length,
                             gfloat            size,
                             gfloat            spacing,
                             LrgTextDirection  direction,
                             gfloat           *width,
                             gfloat           *height)
{
	g_autoptr(GArray) placed = NULL;
	guint             lines = 1;
	gfloat            w = 0.0f;

	g_return_if_fail (LRG_IS_PAINT_FONT (self));
	g_return_if_fail (text != NULL);

	if (ensure_face (self))
	{
		placed = g_array_new (FALSE, FALSE, sizeof (Placed));
		w = layout_text (self, text, length, size, spacing, direction, placed, &lines);
	}

	if (width != NULL)
		*width = w;
	if (height != NULL)
		*height = size + (gfloat) (lines - 1) * size * LINE_HEIGHT_FACTOR;
}

guint
lrg_paint_font_prepare (LrgPaintFont *self,
                        const gchar  *text)
{
	g_autoptr(GArray) placed = NULL;
	guint             made = 0;
	guint             i;

	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0);
	g_return_val_if_fail (text != NULL, 0);

	if (!ensure_face (self))
		return 0;

	placed = g_array_new (FALSE, FALSE, sizeof (Placed));
	layout_text (self, text, -1, (gfloat) self->base_size, 0.0f,
	             LRG_TEXT_DIRECTION_AUTO, placed, NULL);
	for (i = 0; i < placed->len; i++)
	{
		Placed *p = &g_array_index (placed, Placed, i);
		Glyph  *glyph;

		if (p->font == NULL)
			continue;
		glyph = get_glyph (p->font, p->cp);
		if (glyph != NULL && !glyph->rasterised && rasterise (p->font, p->cp, glyph) && glyph->ink)
			made++;
	}
	return made;
}

/* ==========================================================================
 * Drawing
 * ========================================================================== */

/* emit_glyph: one glyph instance (or its shadow) */
static void
emit_glyph (LrgPaintList            *list,
            LrgPaintFont            *self,
            const Placed            *p,
            const Glyph             *glyph,
            gfloat                   origin_x,
            gfloat                   origin_y,
            gfloat                   size,
            gfloat                   em,
            guint                    texture,
            const LrgPaintTextStyle *style,
            gboolean                 shadow)
{
	LrgPaintFont *font = p->font;
	gfloat        k;
	gfloat        baseline;
	gfloat        rect[4];
	gfloat        atlas[4];

	/* canvas units per base pixel of this face at this em */
	k = unit_scale (font, em) / font->base_scale;
	baseline = origin_y + p->y + lrg_paint_font_get_ascent (self, size);

	rect[0] = origin_x + p->x + (gfloat) glyph->off_x * k;
	rect[1] = baseline + (gfloat) (glyph->off_y - font->base_ascent) * k;
	rect[2] = rect[0] + (gfloat) glyph->w * k;
	rect[3] = rect[1] + (gfloat) glyph->h * k;
	atlas[0] = (gfloat) glyph->ax;
	atlas[1] = (gfloat) glyph->ay;
	atlas[2] = (gfloat) (glyph->ax + glyph->w);
	atlas[3] = (gfloat) (glyph->ay + glyph->h);

	if (shadow)
	{
		rect[0] += style->shadow_offset_x;
		rect[2] += style->shadow_offset_x;
		rect[1] += style->shadow_offset_y;
		rect[3] += style->shadow_offset_y;
		_lrg_paint_list_glyph (list, rect, atlas, texture,
		                       (gfloat) LRG_PAINT_FONT_SDF_SPREAD * k,
		                       MAX (style->shadow_softness, 0.0f),
		                       MAX (style->outline_width, 0.0f),
		                       0.0f, 0.0f, &style->shadow_color, NULL, NULL);
		return;
	}

	_lrg_paint_list_glyph (list, rect, atlas, texture,
	                       (gfloat) LRG_PAINT_FONT_SDF_SPREAD * k, 0.0f, 0.0f,
	                       MAX (style->outline_width, 0.0f), MAX (style->glow_radius, 0.0f),
	                       &style->color, &style->outline_color, &style->glow_color);
}

void
lrg_paint_font_draw_text (LrgPaintFont            *self,
                          LrgPaintList            *list,
                          const gchar             *text,
                          gssize                   length,
                          gfloat                   x,
                          gfloat                   y,
                          gfloat                   size,
                          const LrgPaintTextStyle *style)
{
	g_autoptr(GArray) placed = NULL;
	LrgPaintTextStyle plain;
	gboolean          has_shadow;
	gboolean          window;
	gfloat            origin_x;
	gfloat            origin_y;
	gfloat            em;
	guint            *textures;
	guint             n_pages;
	guint             i;
	gint              pass;

	g_return_if_fail (LRG_IS_PAINT_FONT (self));
	g_return_if_fail (LRG_IS_PAINT_LIST (list));
	g_return_if_fail (text != NULL);

	if (style == NULL)
	{
		lrg_paint_text_style_init (&plain, NULL);
		style = &plain;
	}
	if (size <= 0.0f || length == 0 || text[0] == '\0' || !ensure_face (self))
		return;

	has_shadow = style->shadow_color.a > 0;
	if (style->color.a == 0 && !has_shadow &&
	    (style->outline_width <= 0.0f || style->outline_color.a == 0) &&
	    (style->glow_radius <= 0.0f || style->glow_color.a == 0))
		return;

	_lrg_paint_list_keep (list, self);

	/* 1. Layout (all lines) */
	placed = g_array_new (FALSE, FALSE, sizeof (Placed));
	layout_text (self, text, length, size, style->spacing, style->direction, placed, NULL);

	/* 2. Rasterise what is missing before taking any texture id, so a
	 * page that grows does so before this call references it */
	for (i = 0; i < placed->len; i++)
	{
		Placed *p = &g_array_index (placed, Placed, i);
		Glyph  *glyph;

		if (p->font == NULL)
			continue;
		glyph = get_glyph (p->font, p->cp);
		if (glyph != NULL && !glyph->rasterised)
			rasterise (p->font, p->cp, glyph);
	}

	/* 3. One texture lookup (and upload) per page */
	window = IsWindowReady ();
	n_pages = lrg_paint_font_atlas_get_n_pages ();
	textures = g_newa (guint, MAX (n_pages, 1));
	for (i = 0; i < n_pages; i++)
		textures[i] = atlas_page_texture (i);

	/* 4. Instances: all shadows first so no shadow covers a glyph, then
	 * the glyphs. The origin is snapped like the bitmap path */
	origin_x = floorf (x + 0.5f);
	origin_y = floorf (y + 0.5f);
	em = em_px (self, size);

	for (pass = has_shadow ? 0 : 1; pass < 2; pass++)
	{
		for (i = 0; i < placed->len; i++)
		{
			Placed *p = &g_array_index (placed, Placed, i);
			Glyph  *glyph;

			if (p->font == NULL)
			{
				/* No font has it: an empty box ("tofu") */
				if (pass == 1 && p->cp != ' ' && style->color.a > 0)
				{
					gfloat bw = p->advance * 0.8f;
					gfloat bh = lrg_paint_font_get_ascent (self, size) * 0.85f;
					gfloat by = origin_y + p->y + lrg_paint_font_get_ascent (self, size) - bh;

					lrg_paint_list_border (list, origin_x + p->x + p->advance * 0.1f, by,
					                       bw, bh, 0.0f, MAX (size / 16.0f, 1.0f),
					                       &style->color);
				}
				continue;
			}

			glyph = get_glyph (p->font, p->cp);
			if (glyph == NULL || !glyph->ink)
				continue;
			if (window && (glyph->page >= n_pages || textures[glyph->page] == 0))
				continue;

			emit_glyph (list, self, p, glyph, origin_x, origin_y, size, em,
			            glyph->page < n_pages ? textures[glyph->page] : 0,
			            style, pass == 0);
		}
	}
}

/* ==========================================================================
 * System fonts
 * ========================================================================== */

/* Well-known install locations per family (Fedora, Debian, Arch) */
typedef struct
{
	const gchar *family;
	const gchar *paths[6];
} KnownFont;

static const KnownFont known_fonts[] = {
	{ "Noto Sans", {
		"/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
		"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/noto/NotoSans-Regular.ttf", NULL } },
	{ "DejaVu Sans", {
		"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/usr/share/fonts/dejavu/DejaVuSans.ttf", NULL } },
	{ "Adwaita Sans", {
		"/usr/share/fonts/adwaita-sans-fonts/AdwaitaSans-Regular.ttf", NULL } },
	{ "Cantarell", {
		"/usr/share/fonts/abattis-cantarell-vf-fonts/Cantarell-VF.otf",
		"/usr/share/fonts/abattis-cantarell-fonts/Cantarell-Regular.otf",
		"/usr/share/fonts/cantarell/Cantarell-VF.otf", NULL } },
	{ "Liberation Sans", {
		"/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", NULL } },
	{ "Noto Sans Symbols 2", {
		"/usr/share/fonts/google-noto/NotoSansSymbols2-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
		"/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf", NULL } },
	{ "Noto Sans Symbols", {
		"/usr/share/fonts/google-noto-vf/NotoSansSymbols[wght].ttf",
		"/usr/share/fonts/google-noto/NotoSansSymbols-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf", NULL } },
	{ "Noto Sans Arabic", {
		"/usr/share/fonts/google-noto-vf/NotoSansArabic[wght].ttf",
		"/usr/share/fonts/google-noto/NotoSansArabic-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf", NULL } },
	{ "Noto Sans Hebrew", {
		"/usr/share/fonts/google-noto-vf/NotoSansHebrew[wght].ttf",
		"/usr/share/fonts/google-noto/NotoSansHebrew-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSansHebrew-Regular.ttf", NULL } },
	{ "Noto Emoji", {
		"/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji-Regular.ttf",
		"/usr/share/fonts/google-noto-emoji-fonts/NotoEmoji[wght].ttf",
		"/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
		"/usr/share/fonts/noto/NotoEmoji-Regular.ttf", NULL } },
	{ "Noto Sans CJK", {
		"/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
		"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", NULL } },
	{ "Droid Sans Fallback", {
		"/usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf",
		"/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf", NULL } },
	{ "Symbola", {
		"/usr/share/fonts/gdouros-symbola/Symbola.ttf",
		"/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
		"/usr/share/fonts/TTF/Symbola.ttf", NULL } },
	{ NULL, { NULL } }
};

/* Families tried, in order, for generic names and the system default */
static const gchar * const ui_families[] = {
	"Noto Sans", "DejaVu Sans", "Adwaita Sans", "Cantarell", "Liberation Sans", NULL
};

/* Fallbacks added by lrg_paint_font_add_system_fallbacks() */
static const gchar * const fallback_families[] = {
	"DejaVu Sans", "Noto Sans Symbols 2", "Noto Sans Symbols", "Noto Sans Arabic",
	"Noto Sans Hebrew", "Noto Emoji", "Noto Sans CJK", "Droid Sans Fallback", "Symbola", NULL
};

/* fontconfig, loaded at run time */
typedef gpointer (*FcNameParseFunc)          (const guchar *name);
typedef gint     (*FcConfigSubstituteFunc)   (gpointer config, gpointer pattern, gint kind);
typedef void     (*FcDefaultSubstituteFunc)  (gpointer pattern);
typedef gpointer (*FcFontMatchFunc)          (gpointer config, gpointer pattern, gint *result);
typedef gint     (*FcPatternGetStringFunc)   (gpointer pattern, const gchar *object, gint n, guchar **s);
typedef gint     (*FcPatternGetIntegerFunc)  (gpointer pattern, const gchar *object, gint n, gint *i);
typedef void     (*FcPatternDestroyFunc)     (gpointer pattern);
typedef gint     (*FcInitFunc)               (void);

static struct
{
	gboolean                tried;
	GModule                *module;
	FcNameParseFunc         name_parse;
	FcConfigSubstituteFunc  config_substitute;
	FcDefaultSubstituteFunc default_substitute;
	FcFontMatchFunc         font_match;
	FcPatternGetStringFunc  get_string;
	FcPatternGetIntegerFunc get_integer;
	FcPatternDestroyFunc    destroy;
} fc;

/* fc_load: open libfontconfig if installed */
static gboolean
fc_load (void)
{
	FcInitFunc init = NULL;

	if (fc.tried)
		return fc.module != NULL;
	fc.tried = TRUE;

	fc.module = g_module_open ("libfontconfig.so.1", G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if (fc.module == NULL)
		return FALSE;

	if (!g_module_symbol (fc.module, "FcInit", (gpointer *) &init) ||
	    !g_module_symbol (fc.module, "FcNameParse", (gpointer *) &fc.name_parse) ||
	    !g_module_symbol (fc.module, "FcConfigSubstitute", (gpointer *) &fc.config_substitute) ||
	    !g_module_symbol (fc.module, "FcDefaultSubstitute", (gpointer *) &fc.default_substitute) ||
	    !g_module_symbol (fc.module, "FcFontMatch", (gpointer *) &fc.font_match) ||
	    !g_module_symbol (fc.module, "FcPatternGetString", (gpointer *) &fc.get_string) ||
	    !g_module_symbol (fc.module, "FcPatternGetInteger", (gpointer *) &fc.get_integer) ||
	    !g_module_symbol (fc.module, "FcPatternDestroy", (gpointer *) &fc.destroy) ||
	    !init ())
	{
		g_module_close (fc.module);
		fc.module = NULL;
		return FALSE;
	}
	return TRUE;
}

/* is_generic: fontconfig aliases whose match may have any family name */
static gboolean
is_generic (const gchar *family)
{
	return g_ascii_strcasecmp (family, "sans-serif") == 0 ||
	       g_ascii_strcasecmp (family, "sans") == 0 ||
	       g_ascii_strcasecmp (family, "serif") == 0 ||
	       g_ascii_strcasecmp (family, "monospace") == 0 ||
	       g_ascii_strcasecmp (family, "system-ui") == 0;
}

/*
 * fc_match:
 *
 * Asks fontconfig for @family. fontconfig always answers with some
 * font, so for a named family the answer is accepted only when its
 * family starts with the requested name.
 */
static gchar *
fc_match (const gchar *family,
          gint        *index)
{
	gpointer pattern;
	gpointer match;
	gint     result = 0;
	guchar  *file = NULL;
	guchar  *found = NULL;
	gint     idx = 0;
	gchar   *path = NULL;

	if (!fc_load ())
		return NULL;

	pattern = fc.name_parse ((const guchar *) family);
	if (pattern == NULL)
		return NULL;
	fc.config_substitute (NULL, pattern, 0);   /* FcMatchPattern */
	fc.default_substitute (pattern);
	match = fc.font_match (NULL, pattern, &result);
	fc.destroy (pattern);
	if (match == NULL)
		return NULL;

	if (fc.get_string (match, "file", 0, &file) == 0 && file != NULL)
	{
		gboolean ok = TRUE;

		if (!is_generic (family) && fc.get_string (match, "family", 0, &found) == 0 &&
		    found != NULL)
			ok = g_ascii_strncasecmp ((const gchar *) found, family, strlen (family)) == 0;
		if (ok)
		{
			path = g_strdup ((const gchar *) file);
			if (fc.get_integer (match, "index", 0, &idx) != 0)
				idx = 0;
		}
	}
	fc.destroy (match);

	/* the high 16 bits carry a variable font's named instance */
	if (index != NULL)
		*index = idx & 0xFFFF;
	return path;
}

/* usable_file: a font file with outlines and no colour tables */
static gboolean
usable_file (const gchar *path,
             gint         index)
{
	g_autoptr(GMappedFile) mapped = NULL;
	Face                   view;
	gboolean               outlines = FALSE;
	guint16                n;
	guint                  i;

	mapped = g_mapped_file_new (path, FALSE, NULL);
	if (mapped == NULL)
		return FALSE;

	memset (&view, 0, sizeof (Face));
	view.data = (const guint8 *) g_mapped_file_get_contents (mapped);
	view.len = g_mapped_file_get_length (mapped);
	if (view.data == NULL || view.len < 12)
		return FALSE;

	/* For a collection look at the face's own table directory. Only
	 * the tags are read, so a view starting at the face is enough
	 * (its table offsets would be relative to the file) */
	if (be32 (&view, 0) == TAG ('t', 't', 'c', 'f'))
	{
		guint32 off = be32 (&view, 12 + 4 * (gsize) MAX (index, 0));

		if ((gsize) off + 12 > view.len)
			return FALSE;
		view.data += off;
		view.len -= off;
	}

	n = be16 (&view, 4);
	if (12 + 16 * (gsize) n > view.len)
		return FALSE;
	for (i = 0; i < n; i++)
	{
		guint32 tag = be32 (&view, 12 + 16 * (gsize) i);

		if (tag == TAG ('g', 'l', 'y', 'f') || tag == TAG ('C', 'F', 'F', ' '))
			outlines = TRUE;
		if (tag == TAG ('C', 'O', 'L', 'R') || tag == TAG ('C', 'B', 'D', 'T') ||
		    tag == TAG ('s', 'b', 'i', 'x') || tag == TAG ('S', 'V', 'G', ' '))
			return FALSE;
	}
	return outlines;
}

/* find_known: first usable well-known path of @family */
static gchar *
find_known (const gchar *family)
{
	guint i;
	guint j;

	for (i = 0; known_fonts[i].family != NULL; i++)
	{
		if (g_ascii_strcasecmp (known_fonts[i].family, family) != 0)
			continue;
		for (j = 0; j < G_N_ELEMENTS (known_fonts[i].paths) && known_fonts[i].paths[j] != NULL; j++)
			if (g_file_test (known_fonts[i].paths[j], G_FILE_TEST_IS_REGULAR) &&
			    usable_file (known_fonts[i].paths[j], 0))
				return g_strdup (known_fonts[i].paths[j]);
	}
	return NULL;
}

gchar *
lrg_paint_font_find_file (const gchar *family,
                          gint        *face_index)
{
	g_autofree gchar *path = NULL;
	gint              index = 0;
	guint             i;

	g_return_val_if_fail (family != NULL, NULL);

	if (face_index != NULL)
		*face_index = 0;

	/* 1. fontconfig, honouring the user's configuration */
	path = fc_match (family, &index);
	if (path != NULL && usable_file (path, index))
	{
		if (face_index != NULL)
			*face_index = index;
		return g_steal_pointer (&path);
	}

	/* 2. Known locations; generic names try the UI families in order */
	if (!is_generic (family))
		return find_known (family);
	for (i = 0; ui_families[i] != NULL; i++)
	{
		gchar *found = find_known (ui_families[i]);

		if (found != NULL)
			return found;
	}
	return NULL;
}

LrgPaintFont *
lrg_paint_font_new_for_family (const gchar  *family,
                               GError      **error)
{
	g_autofree gchar *path = NULL;
	gint              index = 0;

	g_return_val_if_fail (family != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	path = lrg_paint_font_find_file (family, &index);
	if (path == NULL)
	{
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
		             "no installed font matches \"%s\"", family);
		return NULL;
	}
	return lrg_paint_font_new_from_file_index (path, index, 0, error);
}

/* Shared fallback faces, by path and face index */
static GHashTable *shared_fonts = NULL;

static LrgPaintFont *
shared_font (const gchar *path,
             gint         index)
{
	g_autofree gchar *key = g_strdup_printf ("%s#%d", path, index);
	LrgPaintFont     *font;

	if (shared_fonts == NULL)
		shared_fonts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	font = g_hash_table_lookup (shared_fonts, key);
	if (font == NULL)
	{
		font = font_new_lazy (path, index, 0);
		g_hash_table_insert (shared_fonts, g_steal_pointer (&key), font);
	}
	return font;
}

guint
lrg_paint_font_add_system_fallbacks (LrgPaintFont *self)
{
	guint added = 0;
	guint i;

	g_return_val_if_fail (LRG_IS_PAINT_FONT (self), 0);

	for (i = 0; fallback_families[i] != NULL; i++)
	{
		g_autofree gchar *path = NULL;
		gint              index = 0;
		LrgPaintFont     *font;
		guint             before;

		path = lrg_paint_font_find_file (fallback_families[i], &index);
		if (path == NULL)
			continue;
		/* skip the primary face itself */
		if (self->path != NULL && g_strcmp0 (self->path, path) == 0 && self->face_index == index)
			continue;

		font = shared_font (path, index);
		before = self->fallbacks->len;
		lrg_paint_font_add_fallback (self, font);
		if (self->fallbacks->len > before)
			added++;
	}
	return added;
}

LrgPaintFont *
lrg_paint_font_get_system_default (void)
{
	static gboolean      tried = FALSE;
	static LrgPaintFont *system_font = NULL;

	if (!tried)
	{
		g_autofree gchar *path = NULL;
		g_autoptr(GError) error = NULL;
		gint              index = 0;

		tried = TRUE;
		path = lrg_paint_font_find_file ("sans-serif", &index);
		if (path != NULL)
			system_font = lrg_paint_font_new_from_file_index (path, index, 0, &error);
		if (system_font != NULL)
		{
			lrg_paint_font_add_system_fallbacks (system_font);
			lrg_debug (LRG_LOG_DOMAIN_PAINT, "system UI font: %s (%u fallbacks)",
			           path, system_font->fallbacks->len);
		}
		else
			lrg_debug (LRG_LOG_DOMAIN_PAINT, "no system UI font found%s%s",
			           error != NULL ? ": " : "", error != NULL ? error->message : "");
	}
	return system_font;
}

/* ==========================================================================
 * Defaults and GrlFont binding
 * ========================================================================== */

LrgPaintFont *
lrg_paint_font_get_default (void)
{
	return default_font;
}

void
lrg_paint_font_set_default (LrgPaintFont *font)
{
	g_return_if_fail (font == NULL || LRG_IS_PAINT_FONT (font));

	g_set_object (&default_font, font);
}

void
lrg_paint_font_bind (LrgPaintFont *self,
                     GrlFont      *font)
{
	g_return_if_fail (self == NULL || LRG_IS_PAINT_FONT (self));
	g_return_if_fail (GRL_IS_FONT (font));

	if (bind_quark == 0)
		bind_quark = g_quark_from_static_string ("lrg-paint-font-binding");

	if (self == NULL)
		g_object_set_qdata (G_OBJECT (font), bind_quark, NULL);
	else
		g_object_set_qdata_full (G_OBJECT (font), bind_quark, g_object_ref (self),
		                         g_object_unref);
}

LrgPaintFont *
lrg_paint_font_lookup (GrlFont *font)
{
	if (font == NULL)
		return default_font;
	if (bind_quark == 0)
		return NULL;
	return g_object_get_qdata (G_OBJECT (font), bind_quark);
}
