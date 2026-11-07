/* lrg-terrain-asset.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgTerrainAsset: the `.rterrain` heightfield terrain asset, its layers,
 * edits, PNG sidecars, chunk meshes, splat bake, heightfield physics
 * shape and ray pick.  See lrg-terrain-asset.h and
 * docs/modules/graphics/terrain-asset.org.
 *
 * Layout of the data (nx = chunks_x * resolution + 1 samples along X,
 * nz along Z; cells are (nx - 1) x (nz - 1)):
 *
 *   heights   nx * nz floats, row major (z * nx + x)
 *   weights   nx * nz * LRG_TERRAIN_WEIGHT_STRIDE bytes; a sample's
 *             weights sum to 255 over the used layers
 *   ids       cells bytes: the layer of a cell in material-ids mode
 *   holes     cells bytes: non-zero = hole
 *
 * The foliage density maps live in the asset's LrgFoliage (one byte per
 * cell per foliage layer).
 */

#include "config.h"

#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <gio/gio.h>
#include <raylib.h>

#include "lrg-terrain-asset.h"
#include "lrg-terrain-asset-private.h"
#include "lrg-foliage.h"
#include "../core/lrg-data-loader.h"
#include "../physics/lrg-physics-shape.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Error domain and enums
 * ========================================================================== */

G_DEFINE_QUARK (lrg-terrain-asset-error-quark, lrg_terrain_asset_error)

GType
lrg_terrain_asset_error_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_ASSET_ERROR_PARSE, "LRG_TERRAIN_ASSET_ERROR_PARSE", "parse" },
			{ LRG_TERRAIN_ASSET_ERROR_INVALID, "LRG_TERRAIN_ASSET_ERROR_INVALID", "invalid" },
			{ LRG_TERRAIN_ASSET_ERROR_IO, "LRG_TERRAIN_ASSET_ERROR_IO", "io" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainAssetError"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_terrain_height_format_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_HEIGHT_FORMAT_PNG16, "LRG_TERRAIN_HEIGHT_FORMAT_PNG16", "png16" },
			{ LRG_TERRAIN_HEIGHT_FORMAT_R32, "LRG_TERRAIN_HEIGHT_FORMAT_R32", "r32" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainHeightFormat"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_terrain_splat_mode_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_SPLAT_MODE_WEIGHTS, "LRG_TERRAIN_SPLAT_MODE_WEIGHTS", "weights" },
			{ LRG_TERRAIN_SPLAT_MODE_IDS, "LRG_TERRAIN_SPLAT_MODE_IDS", "ids" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainSplatMode"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

GType
lrg_terrain_edit_kind_get_type (void)
{
	static gsize type_id = 0;

	if (g_once_init_enter (&type_id))
	{
		static const GEnumValue values[] = {
			{ LRG_TERRAIN_EDIT_HEIGHTS, "LRG_TERRAIN_EDIT_HEIGHTS", "heights" },
			{ LRG_TERRAIN_EDIT_SPLAT, "LRG_TERRAIN_EDIT_SPLAT", "splat" },
			{ LRG_TERRAIN_EDIT_MATERIAL_IDS, "LRG_TERRAIN_EDIT_MATERIAL_IDS", "material-ids" },
			{ LRG_TERRAIN_EDIT_HOLES, "LRG_TERRAIN_EDIT_HOLES", "holes" },
			{ LRG_TERRAIN_EDIT_FOLIAGE, "LRG_TERRAIN_EDIT_FOLIAGE", "foliage" },
			{ 0, NULL, NULL }
		};
		GType t = g_enum_register_static (g_intern_static_string ("LrgTerrainEditKind"), values);

		g_once_init_leave (&type_id, t);
	}
	return (GType) type_id;
}

/* ==========================================================================
 * Strict YAML reading (shared, see lrg-terrain-asset-private.h)
 * ========================================================================== */

gboolean
_lrg_terrain_rd_fail (TerrainReader *rd,
                      const gchar   *path,
                      const gchar   *format,
                      ...)
{
	g_autofree gchar *msg = NULL;
	va_list           args;

	va_start (args, format);
	msg = g_strdup_vprintf (format, args);
	va_end (args);
	g_set_error (rd->error, rd->domain, rd->code, "%s: %s: %s", rd->source, path, msg);
	return FALSE;
}

/* A short word for what a node is, for messages. */
static const gchar *
node_kind (YamlNode *node)
{
	if (node == NULL)
		return "nothing";
	switch (yaml_node_get_node_type (node))
	{
	case YAML_NODE_MAPPING:
		return "a mapping";
	case YAML_NODE_SEQUENCE:
		return "a list";
	case YAML_NODE_NULL:
		return "null";
	case YAML_NODE_SCALAR:
	default:
		return "a scalar";
	}
}

gboolean
_lrg_terrain_rd_mapping (TerrainReader  *rd,
                         YamlNode       *node,
                         const gchar    *path,
                         YamlMapping   **out)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_MAPPING)
		return _lrg_terrain_rd_fail (rd, path, "expected a mapping, got %s", node_kind (node));
	*out = yaml_node_get_mapping (node);
	return TRUE;
}

gboolean
_lrg_terrain_rd_sequence (TerrainReader  *rd,
                          YamlNode       *node,
                          const gchar    *path,
                          YamlSequence  **out)
{
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SEQUENCE)
		return _lrg_terrain_rd_fail (rd, path, "expected a list, got %s", node_kind (node));
	*out = yaml_node_get_sequence (node);
	return TRUE;
}

gboolean
_lrg_terrain_rd_number (TerrainReader *rd,
                        YamlNode      *node,
                        const gchar   *path,
                        gdouble        min,
                        gdouble        max,
                        gfloat        *out)
{
	const gchar *text;
	gchar       *end = NULL;
	gdouble      v;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return _lrg_terrain_rd_fail (rd, path, "expected a number, got %s", node_kind (node));
	text = yaml_node_get_scalar (node);
	if (text == NULL || text[0] == '\0')
		return _lrg_terrain_rd_fail (rd, path, "expected a number");
	v = g_ascii_strtod (text, &end);
	if (end == NULL || *end != '\0' || !isfinite (v))
		return _lrg_terrain_rd_fail (rd, path, "expected a number, got \"%s\"", text);
	if (v < min || v > max)
		return _lrg_terrain_rd_fail (rd, path, "expected a number from %g to %g, got %s", min, max, text);
	*out = (gfloat) v;
	return TRUE;
}

gboolean
_lrg_terrain_rd_uint (TerrainReader *rd,
                      YamlNode      *node,
                      const gchar   *path,
                      guint          min,
                      guint          max,
                      guint         *out)
{
	const gchar *text;
	gchar       *end = NULL;
	guint64      v;

	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return _lrg_terrain_rd_fail (rd, path, "expected a whole number, got %s", node_kind (node));
	text = yaml_node_get_scalar (node);
	if (text == NULL || !g_ascii_isdigit (text[0]))
		return _lrg_terrain_rd_fail (rd, path, "expected a whole number from %u to %u", min, max);
	v = g_ascii_strtoull (text, &end, 10);
	if (end == NULL || *end != '\0' || v < (guint64) min || v > (guint64) max)
		return _lrg_terrain_rd_fail (rd, path, "expected a whole number from %u to %u, got \"%s\"", min, max,
		                             text);
	*out = (guint) v;
	return TRUE;
}

gboolean
_lrg_terrain_rd_string (TerrainReader  *rd,
                        YamlNode       *node,
                        const gchar    *path,
                        const gchar   **out)
{
	const gchar *text;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_NULL)
	{
		*out = NULL;
		return TRUE;
	}
	if (node == NULL || yaml_node_get_node_type (node) != YAML_NODE_SCALAR)
		return _lrg_terrain_rd_fail (rd, path, "expected a string, got %s", node_kind (node));
	text = yaml_node_get_scalar (node);
	*out = (text != NULL && text[0] != '\0') ? text : NULL;
	return TRUE;
}

gboolean
_lrg_terrain_rd_boolean (TerrainReader *rd,
                         YamlNode      *node,
                         const gchar   *path,
                         gboolean      *out)
{
	const gchar *text = NULL;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		text = yaml_node_get_scalar (node);
	if (g_strcmp0 (text, "true") == 0)
		*out = TRUE;
	else if (g_strcmp0 (text, "false") == 0)
		*out = FALSE;
	else
		return _lrg_terrain_rd_fail (rd, path, "expected true or false");
	return TRUE;
}

gboolean
_lrg_terrain_rd_numbers (TerrainReader *rd,
                         YamlNode      *node,
                         const gchar   *path,
                         guint          min_count,
                         guint          max_count,
                         gfloat        *out,
                         guint         *out_count)
{
	YamlSequence *seq;
	guint         i, n;

	if (!_lrg_terrain_rd_sequence (rd, node, path, &seq))
		return FALSE;
	n = yaml_sequence_get_length (seq);
	if (n < min_count || n > max_count)
	{
		if (min_count == max_count)
			return _lrg_terrain_rd_fail (rd, path, "expected a list of %u numbers, got %u", min_count, n);
		return _lrg_terrain_rd_fail (rd, path, "expected a list of %u to %u numbers, got %u", min_count,
		                             max_count, n);
	}
	for (i = 0; i < n; i++)
	{
		g_autofree gchar *p = g_strdup_printf ("%s[%u]", path, i);

		if (!_lrg_terrain_rd_number (rd, yaml_sequence_get_element (seq, i), p, -G_MAXFLOAT, G_MAXFLOAT,
		                             &out[i]))
			return FALSE;
	}
	if (out_count != NULL)
		*out_count = n;
	return TRUE;
}

gboolean
_lrg_terrain_rd_enum (TerrainReader *rd,
                      YamlNode      *node,
                      const gchar   *path,
                      GType          enum_type,
                      gint          *out)
{
	g_autoptr(GEnumClass)  klass = g_type_class_ref (enum_type);
	g_autoptr(GString)     names = g_string_new (NULL);
	const gchar           *text = NULL;
	GEnumValue            *ev;
	guint                  i;

	if (node != NULL && yaml_node_get_node_type (node) == YAML_NODE_SCALAR)
		text = yaml_node_get_scalar (node);
	ev = (text != NULL) ? g_enum_get_value_by_nick (klass, text) : NULL;
	if (ev != NULL)
	{
		*out = ev->value;
		return TRUE;
	}
	for (i = 0; i < klass->n_values; i++)
		g_string_append_printf (names, "%s%s", i > 0 ? ", " : "", klass->values[i].value_nick);
	return _lrg_terrain_rd_fail (rd, path, "expected one of %s", names->str);
}

void
_lrg_terrain_keep_extra (YamlMapping **extra,
                         const gchar  *key,
                         YamlNode     *member)
{
	if (*extra == NULL)
		*extra = yaml_mapping_new ();
	yaml_mapping_set_member (*extra, key, member);
}

void
_lrg_terrain_write_extra (YamlMapping *map,
                          YamlMapping *extra)
{
	guint i;

	if (extra == NULL)
		return;
	for (i = 0; i < yaml_mapping_get_size (extra); i++)
		yaml_mapping_set_member (map, yaml_mapping_get_key (extra, i), yaml_mapping_get_value (extra, i));
}

void
_lrg_terrain_set_member (YamlMapping *map,
                         const gchar *key,
                         YamlNode    *node)
{
	yaml_mapping_set_member (map, key, node);
	yaml_node_unref (node);
}

YamlNode *
_lrg_terrain_float_list (const gfloat *values,
                         guint         n)
{
	YamlNode     *node = yaml_node_new_sequence (NULL);
	YamlSequence *seq = yaml_node_get_sequence (node);
	guint         i;

	for (i = 0; i < n; i++)
	{
		YamlNode *v = lrg_data_yaml_float (values[i]);

		yaml_sequence_add_element (seq, v);
		yaml_node_unref (v);
	}
	return node;
}

const gchar *
_lrg_terrain_enum_nick (GType enum_type,
                        gint  value)
{
	g_autoptr(GEnumClass) klass = g_type_class_ref (enum_type);
	GEnumValue           *ev = g_enum_get_value (klass, value);

	/* nicks are static strings of the registered type: they outlive the
	 * class reference */
	return ev != NULL ? ev->value_nick : "";
}

/* ==========================================================================
 * PNG sidecars
 * ========================================================================== */

static const guint8 png_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

/* The CRC-32 of PNG chunks (polynomial 0xedb88320). */
static guint32
png_crc (guint32       crc,
         const guint8 *data,
         gsize         len)
{
	static guint32 table[256];
	static gsize   ready = 0;
	gsize          i;

	if (g_once_init_enter (&ready))
	{
		guint32 n, c, k;

		for (n = 0; n < 256; n++)
		{
			c = n;
			for (k = 0; k < 8; k++)
				c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		g_once_init_leave (&ready, 1);
	}
	crc ^= 0xffffffffu;
	for (i = 0; i < len; i++)
		crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
	return crc ^ 0xffffffffu;
}

static void
put_be32 (GByteArray *out,
          guint32     v)
{
	guint8 b[4];

	b[0] = (guint8) (v >> 24);
	b[1] = (guint8) (v >> 16);
	b[2] = (guint8) (v >> 8);
	b[3] = (guint8) v;
	g_byte_array_append (out, b, 4);
}

static guint32
get_be32 (const guint8 *p)
{
	return ((guint32) p[0] << 24) | ((guint32) p[1] << 16) | ((guint32) p[2] << 8) | (guint32) p[3];
}

/* Appends one chunk: length, type, data, CRC over type and data. */
static void
png_chunk (GByteArray   *out,
           const gchar  *type,
           const guint8 *data,
           gsize         len)
{
	guint32 crc;

	put_be32 (out, (guint32) len);
	g_byte_array_append (out, (const guint8 *) type, 4);
	if (len > 0)
		g_byte_array_append (out, data, (guint) len);
	/* png_crc() takes and returns finished CRCs, so the type's CRC
	 * continues over the data (as zlib's crc32() chains) */
	crc = png_crc (0, (const guint8 *) type, 4);
	crc = png_crc (crc, data, len);
	put_be32 (out, crc);
}

/* Runs @in through a zlib converter until it finishes. */
static GBytes *
zlib_run (GConverter    *conv,
          const guint8  *in,
          gsize          in_len,
          GError       **error)
{
	g_autoptr(GByteArray) out = g_byte_array_new ();
	guint8                buf[65536];
	const guint8         *p = in;
	gsize                 left = in_len;
	GConverterResult      r;

	do
	{
		gsize read = 0, written = 0;

		r = g_converter_convert (conv, p, left, buf, sizeof buf, G_CONVERTER_INPUT_AT_END, &read, &written,
		                         error);
		if (r == G_CONVERTER_ERROR)
			return NULL;
		p += read;
		left -= read;
		if (written > 0)
			g_byte_array_append (out, buf, (guint) written);
	}
	while (r != G_CONVERTER_FINISHED);
	return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}

GBytes *
_lrg_terrain_png_encode (guint          width,
                         guint          height,
                         guint          channels,
                         guint          depth,
                         gconstpointer  samples,
                         GError       **error)
{
	g_autoptr(GByteArray)        out = g_byte_array_new ();
	g_autoptr(GZlibCompressor)   comp = NULL;
	g_autoptr(GBytes)            idat = NULL;
	g_autofree guint8           *raw = NULL;
	g_autofree guint8           *prev = NULL;
	guint8                       ihdr[13];
	gsize                        bpp = (gsize) channels * (depth / 8u);
	gsize                        stride = (gsize) width * bpp;
	guint                        y;
	gsize                        i;

	g_return_val_if_fail (width > 0 && height > 0, NULL);
	g_return_val_if_fail ((channels == 1 || channels == 4) && (depth == 8 || depth == 16), NULL);

	/* IHDR: size, depth, colour type (0 gray, 6 RGBA), deflate, filter
	 * method 0, no interlace */
	ihdr[0] = (guint8) (width >> 24);
	ihdr[1] = (guint8) (width >> 16);
	ihdr[2] = (guint8) (width >> 8);
	ihdr[3] = (guint8) width;
	ihdr[4] = (guint8) (height >> 24);
	ihdr[5] = (guint8) (height >> 16);
	ihdr[6] = (guint8) (height >> 8);
	ihdr[7] = (guint8) height;
	ihdr[8] = (guint8) depth;
	ihdr[9] = (guint8) (channels == 1 ? 0 : 6);
	ihdr[10] = 0;
	ihdr[11] = 0;
	ihdr[12] = 0;

	/* Filtered rows: every row uses filter 2 (Up: the byte minus the byte
	 * above; the row above the first is zero), samples big endian */
	raw = g_malloc ((stride + 1) * (gsize) height);
	prev = g_malloc0 (stride);
	for (y = 0; y < height; y++)
	{
		guint8 *row = raw + (gsize) y * (stride + 1);
		guint8 *cur = row + 1;

		row[0] = 2;
		if (depth == 8)
			memcpy (cur, (const guint8 *) samples + (gsize) y * stride, stride);
		else
		{
			const guint16 *s = (const guint16 *) samples + (gsize) y * width * channels;

			for (i = 0; i < (gsize) width * channels; i++)
			{
				cur[i * 2] = (guint8) (s[i] >> 8);
				cur[i * 2 + 1] = (guint8) s[i];
			}
		}
		for (i = 0; i < stride; i++)
		{
			guint8 v = cur[i];

			cur[i] = (guint8) (v - prev[i]);
			prev[i] = v;
		}
	}

	comp = g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_ZLIB, 6);
	idat = zlib_run (G_CONVERTER (comp), raw, (stride + 1) * (gsize) height, error);
	if (idat == NULL)
		return NULL;

	g_byte_array_append (out, png_signature, 8);
	png_chunk (out, "IHDR", ihdr, 13);
	png_chunk (out, "IDAT", g_bytes_get_data (idat, NULL), g_bytes_get_size (idat));
	png_chunk (out, "IEND", NULL, 0);
	return g_byte_array_free_to_bytes (g_steal_pointer (&out));
}

void
_lrg_terrain_image_clear (TerrainImage *image)
{
	g_clear_pointer (&image->data, g_free);
	image->width = image->height = image->channels = image->depth = 0;
}

guint16
_lrg_terrain_image_get16 (const TerrainImage *image,
                          guint               x,
                          guint               y,
                          guint               c)
{
	gsize idx = ((gsize) y * image->width + x) * image->channels + c;

	if (image->depth == 16)
		return ((const guint16 *) image->data)[idx];
	return (guint16) (image->data[idx] * 257u);
}

/* The Paeth predictor of the PNG specification. */
static guint8
paeth (guint8 a,
       guint8 b,
       guint8 c)
{
	gint p = (gint) a + (gint) b - (gint) c;
	gint pa = ABS (p - (gint) a);
	gint pb = ABS (p - (gint) b);
	gint pc = ABS (p - (gint) c);

	if (pa <= pb && pa <= pc)
		return a;
	if (pb <= pc)
		return b;
	return c;
}

gboolean
_lrg_terrain_png_decode (const guint8  *bytes,
                         gsize          length,
                         TerrainImage  *out,
                         GError       **error)
{
	g_autoptr(GByteArray)        idat = g_byte_array_new ();
	g_autoptr(GZlibDecompressor) dec = NULL;
	g_autoptr(GBytes)            raw = NULL;
	g_autofree guint8           *prev = NULL;
	const guint8                *rp;
	gsize                        pos = 8, bpp, stride, raw_len;
	guint                        width = 0, height = 0, depth = 0, ctype = 0, channels = 0;
	gboolean                     have_header = FALSE;
	guint                        y;
	gsize                        i;

	memset (out, 0, sizeof *out);
	if (length < 8 || memcmp (bytes, png_signature, 8) != 0)
	{
		g_set_error_literal (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID, "not a PNG file");
		return FALSE;
	}

	/* Walk the chunks: IHDR, every IDAT, until IEND */
	while (pos + 12 <= length)
	{
		guint32       len = get_be32 (bytes + pos);
		const guint8 *type = bytes + pos + 4;
		const guint8 *data = bytes + pos + 8;

		if ((gsize) len > length - pos - 12)
			break;
		if (memcmp (type, "IHDR", 4) == 0 && len >= 13)
		{
			width = get_be32 (data);
			height = get_be32 (data + 4);
			depth = data[8];
			ctype = data[9];
			if (data[12] != 0)
			{
				g_set_error_literal (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
				                     "interlaced PNG files are not supported");
				return FALSE;
			}
			have_header = TRUE;
		}
		else if (memcmp (type, "IDAT", 4) == 0)
			g_byte_array_append (idat, data, len);
		else if (memcmp (type, "IEND", 4) == 0)
			break;
		pos += (gsize) len + 12;
	}

	switch (ctype)
	{
	case 0:
		channels = 1;
		break;
	case 2:
		channels = 3;
		break;
	case 4:
		channels = 2;
		break;
	case 6:
		channels = 4;
		break;
	default:
		channels = 0;
		break;
	}
	if (!have_header || channels == 0 || (depth != 8 && depth != 16) || width == 0 || height == 0 ||
	    width > 65536 || height > 65536)
	{
		g_set_error_literal (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
		                     "unsupported PNG (8 or 16-bit gray, gray alpha, RGB or RGBA expected)");
		return FALSE;
	}

	dec = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_ZLIB);
	raw = zlib_run (G_CONVERTER (dec), idat->data, idat->len, error);
	if (raw == NULL)
		return FALSE;

	bpp = (gsize) channels * (depth / 8u);
	stride = (gsize) width * bpp;
	rp = g_bytes_get_data (raw, &raw_len);
	if (raw_len < (stride + 1) * (gsize) height)
	{
		g_set_error_literal (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
		                     "truncated PNG image data");
		return FALSE;
	}

	out->width = width;
	out->height = height;
	out->channels = channels;
	out->depth = depth;
	out->data = g_malloc (stride * (gsize) height);
	prev = g_malloc0 (stride);

	/* Unfilter each row into the output (big endian bytes for now) */
	for (y = 0; y < height; y++)
	{
		const guint8 *row = rp + (gsize) y * (stride + 1);
		guint8        filter = row[0];
		guint8       *cur = out->data + (gsize) y * stride;

		for (i = 0; i < stride; i++)
		{
			guint8 a = (i >= bpp) ? cur[i - bpp] : 0;
			guint8 b = prev[i];
			guint8 c = (i >= bpp) ? prev[i - bpp] : 0;
			guint8 x = row[1 + i];

			switch (filter)
			{
			case 0:
				cur[i] = x;
				break;
			case 1:
				cur[i] = (guint8) (x + a);
				break;
			case 2:
				cur[i] = (guint8) (x + b);
				break;
			case 3:
				cur[i] = (guint8) (x + (guint8) (((guint) a + (guint) b) / 2u));
				break;
			case 4:
				cur[i] = (guint8) (x + paeth (a, b, c));
				break;
			default:
				_lrg_terrain_image_clear (out);
				g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
				             "bad PNG row filter %u", (guint) filter);
				return FALSE;
			}
		}
		memcpy (prev, cur, stride);
	}

	/* 16-bit samples to native order */
	if (depth == 16)
	{
		guint16 *s = (guint16 *) out->data;

		for (i = 0; i < stride * (gsize) height / 2; i++)
		{
			const guint8 *b = out->data + i * 2;

			s[i] = (guint16) (((guint) b[0] << 8) | (guint) b[1]);
		}
	}
	return TRUE;
}

gboolean
_lrg_terrain_png_load (const gchar   *path,
                       TerrainImage  *out,
                       GError       **error)
{
	g_autofree gchar *contents = NULL;
	gsize             len = 0;
	g_autoptr(GError) local = NULL;

	if (!g_file_get_contents (path, &contents, &len, &local))
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return _lrg_terrain_png_decode ((const guint8 *) contents, len, out, error);
}

gboolean
_lrg_terrain_png_save (const gchar   *path,
                       guint          width,
                       guint          height,
                       guint          channels,
                       guint          depth,
                       gconstpointer  samples,
                       GError       **error)
{
	g_autoptr(GBytes) png = NULL;
	g_autoptr(GError) local = NULL;
	gsize             len = 0;
	gconstpointer     data;

	png = _lrg_terrain_png_encode (width, height, channels, depth, samples, error);
	if (png == NULL)
		return FALSE;
	data = g_bytes_get_data (png, &len);
	if (!g_file_set_contents (path, data, (gssize) len, &local))
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	return TRUE;
}

/* ==========================================================================
 * LrgTerrainEdit
 * ========================================================================== */

struct _LrgTerrainEdit
{
	LrgTerrainEditKind  kind;
	guint               layer;
	guint               x;
	guint               z;
	guint               width;
	guint               height;
	GBytes             *before;
	GBytes             *after;
};

G_DEFINE_BOXED_TYPE (LrgTerrainEdit, lrg_terrain_edit, lrg_terrain_edit_copy, lrg_terrain_edit_free)

LrgTerrainEdit *
lrg_terrain_edit_copy (const LrgTerrainEdit *edit)
{
	LrgTerrainEdit *c;

	g_return_val_if_fail (edit != NULL, NULL);
	c = g_new0 (LrgTerrainEdit, 1);
	*c = *edit;
	c->before = edit->before != NULL ? g_bytes_ref (edit->before) : NULL;
	c->after = edit->after != NULL ? g_bytes_ref (edit->after) : NULL;
	return c;
}

void
lrg_terrain_edit_free (LrgTerrainEdit *edit)
{
	if (edit == NULL)
		return;
	g_clear_pointer (&edit->before, g_bytes_unref);
	g_clear_pointer (&edit->after, g_bytes_unref);
	g_free (edit);
}

LrgTerrainEdit *
_lrg_terrain_edit_new (LrgTerrainEditKind  kind,
                       guint               layer,
                       guint               x,
                       guint               z,
                       guint               width,
                       guint               height,
                       GBytes             *before,
                       GBytes             *after)
{
	LrgTerrainEdit *edit = g_new0 (LrgTerrainEdit, 1);

	edit->kind = kind;
	edit->layer = layer;
	edit->x = x;
	edit->z = z;
	edit->width = width;
	edit->height = height;
	edit->before = g_bytes_ref (before);
	edit->after = g_bytes_ref (after);
	return edit;
}

gsize
_lrg_terrain_edit_elem_size (LrgTerrainEditKind kind)
{
	switch (kind)
	{
	case LRG_TERRAIN_EDIT_HEIGHTS:
		return sizeof (gfloat);
	case LRG_TERRAIN_EDIT_SPLAT:
		return LRG_TERRAIN_WEIGHT_STRIDE;
	case LRG_TERRAIN_EDIT_MATERIAL_IDS:
	case LRG_TERRAIN_EDIT_HOLES:
	case LRG_TERRAIN_EDIT_FOLIAGE:
	default:
		return 1;
	}
}

LrgTerrainEditKind
lrg_terrain_edit_get_kind (const LrgTerrainEdit *edit)
{
	g_return_val_if_fail (edit != NULL, LRG_TERRAIN_EDIT_HEIGHTS);
	return edit->kind;
}

guint
lrg_terrain_edit_get_layer (const LrgTerrainEdit *edit)
{
	g_return_val_if_fail (edit != NULL, 0);
	return edit->layer;
}

void
lrg_terrain_edit_get_region (const LrgTerrainEdit *edit,
                             guint                *x,
                             guint                *z,
                             guint                *width,
                             guint                *height)
{
	g_return_if_fail (edit != NULL);
	if (x != NULL)
		*x = edit->x;
	if (z != NULL)
		*z = edit->z;
	if (width != NULL)
		*width = edit->width;
	if (height != NULL)
		*height = edit->height;
}

gboolean
lrg_terrain_edit_is_empty (const LrgTerrainEdit *edit)
{
	g_return_val_if_fail (edit != NULL, TRUE);
	if (edit->width == 0 || edit->height == 0)
		return TRUE;
	return g_bytes_equal (edit->before, edit->after);
}

GBytes *
lrg_terrain_edit_get_before (const LrgTerrainEdit *edit)
{
	g_return_val_if_fail (edit != NULL, NULL);
	return edit->before;
}

GBytes *
lrg_terrain_edit_get_after (const LrgTerrainEdit *edit)
{
	g_return_val_if_fail (edit != NULL, NULL);
	return edit->after;
}

/* ==========================================================================
 * LrgTerrainLayer
 * ========================================================================== */

struct _LrgTerrainLayer
{
	GObject      parent_instance;

	gchar       *name;
	gchar       *texture;
	gchar       *material;
	gfloat       tiling;
	gfloat       color[4];
	YamlMapping *extra;        /* unknown keys of the last load */

	/* albedo pixels for the bake: set, or loaded from the texture */
	guint8      *pixels;
	guint        pixels_w;
	guint        pixels_h;
	gboolean     pixels_set;   /* given by lrg_terrain_layer_set_pixels() */
	gboolean     load_tried;
};

G_DEFINE_FINAL_TYPE (LrgTerrainLayer, lrg_terrain_layer, G_TYPE_OBJECT)

enum
{
	LAYER_PROP_0,
	LAYER_PROP_NAME,
	LAYER_PROP_TEXTURE,
	LAYER_PROP_MATERIAL,
	LAYER_PROP_TILING,
	LAYER_N_PROPS
};

static GParamSpec *layer_props[LAYER_N_PROPS];

static void
lrg_terrain_layer_finalize (GObject *object)
{
	LrgTerrainLayer *self = LRG_TERRAIN_LAYER (object);

	g_free (self->name);
	g_free (self->texture);
	g_free (self->material);
	g_free (self->pixels);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	G_OBJECT_CLASS (lrg_terrain_layer_parent_class)->finalize (object);
}

static void
lrg_terrain_layer_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
	LrgTerrainLayer *self = LRG_TERRAIN_LAYER (object);

	switch (prop_id)
	{
	case LAYER_PROP_NAME:
		g_value_set_string (value, self->name);
		break;
	case LAYER_PROP_TEXTURE:
		g_value_set_string (value, self->texture);
		break;
	case LAYER_PROP_MATERIAL:
		g_value_set_string (value, self->material);
		break;
	case LAYER_PROP_TILING:
		g_value_set_float (value, self->tiling);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_layer_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
	LrgTerrainLayer *self = LRG_TERRAIN_LAYER (object);

	switch (prop_id)
	{
	case LAYER_PROP_NAME:
		lrg_terrain_layer_set_name (self, g_value_get_string (value));
		break;
	case LAYER_PROP_TEXTURE:
		lrg_terrain_layer_set_texture (self, g_value_get_string (value));
		break;
	case LAYER_PROP_MATERIAL:
		lrg_terrain_layer_set_material (self, g_value_get_string (value));
		break;
	case LAYER_PROP_TILING:
		lrg_terrain_layer_set_tiling (self, g_value_get_float (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
lrg_terrain_layer_class_init (LrgTerrainLayerClass *klass)
{
	GObjectClass     *object_class = G_OBJECT_CLASS (klass);
	const GParamFlags rw = G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY;

	object_class->finalize = lrg_terrain_layer_finalize;
	object_class->get_property = lrg_terrain_layer_get_property;
	object_class->set_property = lrg_terrain_layer_set_property;

	/**
	 * LrgTerrainLayer:name:
	 *
	 * The layer's display name.
	 */
	layer_props[LAYER_PROP_NAME] = g_param_spec_string ("name", NULL, "Layer name", NULL, rw);

	/**
	 * LrgTerrainLayer:texture:
	 *
	 * The albedo texture reference (path or `guid:`).
	 */
	layer_props[LAYER_PROP_TEXTURE] = g_param_spec_string ("texture", NULL, "Albedo texture", NULL, rw);

	/**
	 * LrgTerrainLayer:material:
	 *
	 * The `.rmat` reference kept for the layer.
	 */
	layer_props[LAYER_PROP_MATERIAL] = g_param_spec_string ("material", NULL, "Material", NULL, rw);

	/**
	 * LrgTerrainLayer:tiling:
	 *
	 * World units one texture repeat covers.
	 */
	layer_props[LAYER_PROP_TILING] =
		g_param_spec_float ("tiling", NULL, "World units per texture repeat", 0.001f, 100000.0f, 4.0f, rw);

	g_object_class_install_properties (object_class, LAYER_N_PROPS, layer_props);
}

static void
lrg_terrain_layer_init (LrgTerrainLayer *self)
{
	self->tiling = 4.0f;
	self->color[0] = self->color[1] = self->color[2] = self->color[3] = 1.0f;
}

LrgTerrainLayer *
lrg_terrain_layer_new (const gchar *name)
{
	return (LrgTerrainLayer *) g_object_new (LRG_TYPE_TERRAIN_LAYER, "name", name, NULL);
}

const gchar *
lrg_terrain_layer_get_name (LrgTerrainLayer *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_LAYER (self), NULL);
	return self->name;
}

void
lrg_terrain_layer_set_name (LrgTerrainLayer *self,
                            const gchar     *name)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	if (g_strcmp0 (self->name, name) == 0)
		return;
	g_free (self->name);
	self->name = g_strdup (name);
	g_object_notify_by_pspec (G_OBJECT (self), layer_props[LAYER_PROP_NAME]);
}

const gchar *
lrg_terrain_layer_get_texture (LrgTerrainLayer *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_LAYER (self), NULL);
	return self->texture;
}

void
lrg_terrain_layer_set_texture (LrgTerrainLayer *self,
                               const gchar     *texture)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	if (g_strcmp0 (self->texture, texture) == 0)
		return;
	g_free (self->texture);
	self->texture = g_strdup (texture);
	if (!self->pixels_set)
	{
		g_clear_pointer (&self->pixels, g_free);
		self->pixels_w = self->pixels_h = 0;
	}
	self->load_tried = FALSE;
	g_object_notify_by_pspec (G_OBJECT (self), layer_props[LAYER_PROP_TEXTURE]);
}

const gchar *
lrg_terrain_layer_get_material (LrgTerrainLayer *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_LAYER (self), NULL);
	return self->material;
}

void
lrg_terrain_layer_set_material (LrgTerrainLayer *self,
                                const gchar     *material)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	if (g_strcmp0 (self->material, material) == 0)
		return;
	g_free (self->material);
	self->material = g_strdup (material);
	g_object_notify_by_pspec (G_OBJECT (self), layer_props[LAYER_PROP_MATERIAL]);
}

gfloat
lrg_terrain_layer_get_tiling (LrgTerrainLayer *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_LAYER (self), 4.0f);
	return self->tiling;
}

void
lrg_terrain_layer_set_tiling (LrgTerrainLayer *self,
                              gfloat           tiling)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	if (!(tiling > 0.0f) || !isfinite (tiling))
		tiling = 4.0f;
	if (self->tiling == tiling)
		return;
	self->tiling = tiling;
	g_object_notify_by_pspec (G_OBJECT (self), layer_props[LAYER_PROP_TILING]);
}

void
lrg_terrain_layer_get_color (LrgTerrainLayer *self,
                             gfloat          *out)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	g_return_if_fail (out != NULL);
	memcpy (out, self->color, sizeof self->color);
}

void
lrg_terrain_layer_set_color (LrgTerrainLayer *self,
                             gfloat           r,
                             gfloat           g,
                             gfloat           b,
                             gfloat           a)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));
	self->color[0] = CLAMP (r, 0.0f, 1.0f);
	self->color[1] = CLAMP (g, 0.0f, 1.0f);
	self->color[2] = CLAMP (b, 0.0f, 1.0f);
	self->color[3] = CLAMP (a, 0.0f, 1.0f);
}

void
lrg_terrain_layer_set_pixels (LrgTerrainLayer *self,
                              guint            width,
                              guint            height,
                              const guint8    *rgba)
{
	g_return_if_fail (LRG_IS_TERRAIN_LAYER (self));

	g_clear_pointer (&self->pixels, g_free);
	self->pixels_w = self->pixels_h = 0;
	self->pixels_set = FALSE;
	self->load_tried = FALSE;
	if (rgba == NULL || width == 0 || height == 0)
		return;
	self->pixels = g_memdup2 (rgba, (gsize) width * height * 4);
	self->pixels_w = width;
	self->pixels_h = height;
	self->pixels_set = TRUE;
}

gboolean
lrg_terrain_layer_has_pixels (LrgTerrainLayer *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_LAYER (self), FALSE);
	return self->pixels != NULL;
}

/* Loads the layer's texture file into pixels once (headless: raylib's
 * image loader needs no GL context).  guid: references are left to tools
 * that can resolve them. */
static void
layer_ensure_pixels (LrgTerrainLayer *self,
                     const gchar     *base_dir)
{
	g_autofree gchar *path = NULL;
	Image             img;

	if (self->pixels != NULL || self->load_tried || self->texture == NULL)
		return;
	self->load_tried = TRUE;
	if (g_str_has_prefix (self->texture, "guid:"))
		return;
	if (g_path_is_absolute (self->texture) || base_dir == NULL)
		path = g_strdup (self->texture);
	else
		path = g_build_filename (base_dir, self->texture, NULL);
	if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
	{
		g_debug ("LrgTerrainLayer: texture '%s' not found", path);
		return;
	}
	img = LoadImage (path);
	if (img.data == NULL || img.width <= 0 || img.height <= 0)
		return;
	ImageFormat (&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
	self->pixels = g_memdup2 (img.data, (gsize) img.width * (gsize) img.height * 4);
	self->pixels_w = (guint) img.width;
	self->pixels_h = (guint) img.height;
	UnloadImage (img);
}

/* Bilinear, wrapped sample of the layer's pixels at texture coordinates
 * (u, v); white without pixels. */
static void
layer_sample (LrgTerrainLayer *self,
              gfloat           u,
              gfloat           v,
              gfloat          *out)
{
	gfloat fx, fz, tx, tz;
	gint   x0, z0, x1, z1, c;
	gint   w = (gint) self->pixels_w, h = (gint) self->pixels_h;

	if (self->pixels == NULL)
	{
		out[0] = out[1] = out[2] = 1.0f;
		return;
	}
	fx = (u - floorf (u)) * (gfloat) w - 0.5f;
	fz = (v - floorf (v)) * (gfloat) h - 0.5f;
	x0 = (gint) floorf (fx);
	z0 = (gint) floorf (fz);
	tx = fx - (gfloat) x0;
	tz = fz - (gfloat) z0;
	x0 = ((x0 % w) + w) % w;
	z0 = ((z0 % h) + h) % h;
	x1 = (x0 + 1) % w;
	z1 = (z0 + 1) % h;
	for (c = 0; c < 3; c++)
	{
		gfloat a = self->pixels[((gsize) z0 * (gsize) w + (gsize) x0) * 4 + (gsize) c];
		gfloat b = self->pixels[((gsize) z0 * (gsize) w + (gsize) x1) * 4 + (gsize) c];
		gfloat d = self->pixels[((gsize) z1 * (gsize) w + (gsize) x0) * 4 + (gsize) c];
		gfloat e = self->pixels[((gsize) z1 * (gsize) w + (gsize) x1) * 4 + (gsize) c];

		out[c] = ((a * (1.0f - tx) + b * tx) * (1.0f - tz) + (d * (1.0f - tx) + e * tx) * tz) / 255.0f;
	}
}

/* ==========================================================================
 * LrgTerrainAsset: instance
 * ========================================================================== */

struct _LrgTerrainAsset
{
	GObject                 parent_instance;

	gchar                  *name;
	gchar                  *base_dir;
	gchar                  *stem;           /* file stem of the last load / save */

	guint                   resolution;
	gfloat                  chunk_size;
	guint                   chunks_x;
	guint                   chunks_z;
	guint                   nx;             /* samples */
	guint                   nz;

	gfloat                 *heights;
	guint8                 *weights;
	guint8                 *ids;
	guint8                 *holes;

	LrgTerrainHeightFormat  height_format;
	gfloat                  range_min;
	gfloat                  range_max;
	LrgTerrainSplatMode     splat_mode;
	GPtrArray              *layers;         /* LrgTerrainLayer */

	guint                   lod_levels;
	gfloat                  lod_distances[LRG_TERRAIN_ASSET_MAX_LOD];
	gfloat                  skirt_depth;
	guint                   bake_resolution;

	LrgFoliage             *foliage;

	/* sidecar names (kept from the last load / save) */
	gchar                  *height_file;
	gchar                  *splat_files[2];
	gchar                  *ids_file;
	gchar                  *holes_file;

	/* unknown keys of the last load */
	YamlMapping            *extra;
	YamlMapping            *extra_grid;
	YamlMapping            *extra_heights;
	YamlMapping            *extra_splat;
	YamlMapping            *extra_holes;
	YamlMapping            *extra_lod;
};

G_DEFINE_FINAL_TYPE (LrgTerrainAsset, lrg_terrain_asset, G_TYPE_OBJECT)

enum
{
	SIGNAL_REGION_CHANGED,
	N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_terrain_asset_finalize (GObject *object)
{
	LrgTerrainAsset *self = LRG_TERRAIN_ASSET (object);

	g_free (self->name);
	g_free (self->base_dir);
	g_free (self->stem);
	g_free (self->heights);
	g_free (self->weights);
	g_free (self->ids);
	g_free (self->holes);
	g_ptr_array_unref (self->layers);
	g_clear_object (&self->foliage);
	g_free (self->height_file);
	g_free (self->splat_files[0]);
	g_free (self->splat_files[1]);
	g_free (self->ids_file);
	g_free (self->holes_file);
	if (self->extra != NULL)
		yaml_mapping_unref (self->extra);
	if (self->extra_grid != NULL)
		yaml_mapping_unref (self->extra_grid);
	if (self->extra_heights != NULL)
		yaml_mapping_unref (self->extra_heights);
	if (self->extra_splat != NULL)
		yaml_mapping_unref (self->extra_splat);
	if (self->extra_holes != NULL)
		yaml_mapping_unref (self->extra_holes);
	if (self->extra_lod != NULL)
		yaml_mapping_unref (self->extra_lod);
	G_OBJECT_CLASS (lrg_terrain_asset_parent_class)->finalize (object);
}

static void
lrg_terrain_asset_class_init (LrgTerrainAssetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = lrg_terrain_asset_finalize;

	/**
	 * LrgTerrainAsset::region-changed:
	 * @self: the asset
	 * @kind: the #LrgTerrainEditKind that changed
	 * @layer: the foliage layer (for %LRG_TERRAIN_EDIT_FOLIAGE)
	 * @x: first column (samples or cells, see #LrgTerrainEditKind)
	 * @z: first row
	 * @width: columns
	 * @height: rows
	 *
	 * Emitted once per change of a region of the asset's data (a setter,
	 * a brush dab, an edit applied, a resize).  Renderers re-mesh and
	 * re-bake the chunks it overlaps; the foliage marks its patches.
	 */
	signals[SIGNAL_REGION_CHANGED] =
		g_signal_new ("region-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
		              G_TYPE_NONE, 6, LRG_TYPE_TERRAIN_EDIT_KIND, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
		              G_TYPE_UINT, G_TYPE_UINT);
}

static void
lrg_terrain_asset_init (LrgTerrainAsset *self)
{
	self->layers = g_ptr_array_new_with_free_func (g_object_unref);
	self->foliage = lrg_foliage_new ();
	self->height_format = LRG_TERRAIN_HEIGHT_FORMAT_PNG16;
	self->range_min = 0.0f;
	self->range_max = 1.0f;
	self->lod_levels = 3;
	self->bake_resolution = 256;
}

/* (Re)allocates every array for the current grid: heights 0, layer 0 at
 * full weight, ids 0, no holes. */
static void
alloc_grid (LrgTerrainAsset *self)
{
	gsize n = (gsize) self->nx * self->nz;
	gsize cells = (gsize) (self->nx - 1) * (self->nz - 1);
	gsize i;

	g_free (self->heights);
	g_free (self->weights);
	g_free (self->ids);
	g_free (self->holes);
	self->heights = g_new0 (gfloat, n);
	self->weights = g_malloc0 (n * LRG_TERRAIN_WEIGHT_STRIDE);
	for (i = 0; i < n; i++)
		self->weights[i * LRG_TERRAIN_WEIGHT_STRIDE] = 255;
	self->ids = g_malloc0 (cells);
	self->holes = g_malloc0 (cells);
	_lrg_foliage_set_grid (self->foliage, self->nx - 1, self->nz - 1,
	                       self->chunk_size / (gfloat) self->resolution);
}

/* Sets the grid and the default LOD distances (2, 4, 8 ... chunk sizes). */
static void
set_grid (LrgTerrainAsset *self,
          guint            resolution,
          gfloat           chunk_size,
          guint            chunks_x,
          guint            chunks_z)
{
	guint l;

	self->resolution = CLAMP (resolution, 1u, (guint) LRG_TERRAIN_ASSET_MAX_RESOLUTION);
	self->chunk_size = (chunk_size > 0.0f && isfinite (chunk_size)) ? chunk_size : 32.0f;
	self->chunks_x = CLAMP (chunks_x, 1u, (guint) LRG_TERRAIN_ASSET_MAX_CHUNKS);
	self->chunks_z = CLAMP (chunks_z, 1u, (guint) LRG_TERRAIN_ASSET_MAX_CHUNKS);
	self->nx = self->chunks_x * self->resolution + 1;
	self->nz = self->chunks_z * self->resolution + 1;
	self->skirt_depth = self->chunk_size * 0.1f;
	self->lod_distances[0] = 0.0f;
	for (l = 1; l < LRG_TERRAIN_ASSET_MAX_LOD; l++)
		self->lod_distances[l] = self->chunk_size * (gfloat) (1u << l);
	alloc_grid (self);
}

LrgTerrainAsset *
lrg_terrain_asset_new (guint  resolution,
                       gfloat chunk_size,
                       guint  chunks_x,
                       guint  chunks_z)
{
	LrgTerrainAsset *self = (LrgTerrainAsset *) g_object_new (LRG_TYPE_TERRAIN_ASSET, NULL);

	set_grid (self, resolution, chunk_size, chunks_x, chunks_z);
	g_ptr_array_add (self->layers, lrg_terrain_layer_new ("base"));
	return self;
}

/* ==========================================================================
 * Internals shared with the brush, the foliage and the components
 * ========================================================================== */

gfloat *
_lrg_terrain_asset_heights (LrgTerrainAsset *self)
{
	return self->heights;
}

guint8 *
_lrg_terrain_asset_weights (LrgTerrainAsset *self)
{
	return self->weights;
}

guint8 *
_lrg_terrain_asset_ids (LrgTerrainAsset *self)
{
	return self->ids;
}

guint8 *
_lrg_terrain_asset_holes (LrgTerrainAsset *self)
{
	return self->holes;
}

void
_lrg_terrain_asset_emit_changed (LrgTerrainAsset    *self,
                                 LrgTerrainEditKind  kind,
                                 guint               layer,
                                 guint               x,
                                 guint               z,
                                 guint               width,
                                 guint               height)
{
	guint cx, cz, cw, ch;

	if (width == 0 || height == 0)
		return;

	/* The foliage stands on heights and weights: the cells around changed
	 * samples (and changed cells themselves) need re-scattering */
	if (kind == LRG_TERRAIN_EDIT_HEIGHTS || kind == LRG_TERRAIN_EDIT_SPLAT)
	{
		cx = x > 0 ? x - 1 : 0;
		cz = z > 0 ? z - 1 : 0;
		cw = x + width - cx;
		ch = z + height - cz;
	}
	else
	{
		cx = x;
		cz = z;
		cw = width;
		ch = height;
	}
	lrg_foliage_mark_dirty (self->foliage, cx, cz, cw, ch);
	g_signal_emit (self, signals[SIGNAL_REGION_CHANGED], 0, kind, layer, x, z, width, height);
}

void
_lrg_terrain_normalize_weights (guint8 *w,
                                guint   n_layers)
{
	guint sum = 0, largest = 0, i, total;

	for (i = 0; i < n_layers; i++)
	{
		sum += w[i];
		if (w[i] > w[largest])
			largest = i;
	}
	for (i = n_layers; i < LRG_TERRAIN_WEIGHT_STRIDE; i++)
		w[i] = 0;
	if (sum == 255)
		return;
	if (sum == 0)
	{
		w[0] = 255;
		return;
	}

	/* scale to 255, then give the rounding rest to the largest layer */
	total = 0;
	for (i = 0; i < n_layers; i++)
	{
		w[i] = (guint8) (((guint) w[i] * 255u + sum / 2u) / sum);
		total += w[i];
	}
	if (total > 255)
		w[largest] = (guint8) (w[largest] - (total - 255));
	else
		w[largest] = (guint8) (w[largest] + (255 - total));
}

/* ==========================================================================
 * Accessors
 * ========================================================================== */

const gchar *
lrg_terrain_asset_get_name (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	return self->name;
}

void
lrg_terrain_asset_set_name (LrgTerrainAsset *self,
                            const gchar     *name)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	g_free (self->name);
	self->name = g_strdup (name);
}

const gchar *
lrg_terrain_asset_get_base_dir (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	return self->base_dir;
}

void
lrg_terrain_asset_set_base_dir (LrgTerrainAsset *self,
                                const gchar     *base_dir)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	g_free (self->base_dir);
	self->base_dir = g_strdup (base_dir);
}

guint
lrg_terrain_asset_get_resolution (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	return self->resolution;
}

gfloat
lrg_terrain_asset_get_chunk_size (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	return self->chunk_size;
}

gfloat
lrg_terrain_asset_get_cell_size (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	return self->chunk_size / (gfloat) self->resolution;
}

void
lrg_terrain_asset_get_chunks (LrgTerrainAsset *self,
                              guint           *chunks_x,
                              guint           *chunks_z)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (chunks_x != NULL)
		*chunks_x = self->chunks_x;
	if (chunks_z != NULL)
		*chunks_z = self->chunks_z;
}

void
lrg_terrain_asset_get_samples (LrgTerrainAsset *self,
                               guint           *samples_x,
                               guint           *samples_z)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (samples_x != NULL)
		*samples_x = self->nx;
	if (samples_z != NULL)
		*samples_z = self->nz;
}

void
lrg_terrain_asset_get_cells (LrgTerrainAsset *self,
                             guint           *cells_x,
                             guint           *cells_z)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (cells_x != NULL)
		*cells_x = self->nx - 1;
	if (cells_z != NULL)
		*cells_z = self->nz - 1;
}

void
lrg_terrain_asset_get_size (LrgTerrainAsset *self,
                            gfloat          *width,
                            gfloat          *depth)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (width != NULL)
		*width = self->chunk_size * (gfloat) self->chunks_x;
	if (depth != NULL)
		*depth = self->chunk_size * (gfloat) self->chunks_z;
}

void
lrg_terrain_asset_resize (LrgTerrainAsset *self,
                          guint            chunks_x,
                          guint            chunks_z)
{
	g_autofree gfloat *old_h = NULL;
	g_autofree guint8 *old_w = NULL;
	g_autofree guint8 *old_ids = NULL;
	g_autofree guint8 *old_holes = NULL;
	g_autoptr(GPtrArray) old_density = NULL;
	guint              onx, onz, ocx, ocz, x, z, l, n_fl;

	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	chunks_x = CLAMP (chunks_x, 1u, (guint) LRG_TERRAIN_ASSET_MAX_CHUNKS);
	chunks_z = CLAMP (chunks_z, 1u, (guint) LRG_TERRAIN_ASSET_MAX_CHUNKS);
	if (chunks_x == self->chunks_x && chunks_z == self->chunks_z)
		return;

	/* keep the old data, then copy it into the new grid (new samples take
	 * the nearest old edge sample) */
	onx = self->nx;
	onz = self->nz;
	ocx = onx - 1;
	ocz = onz - 1;
	old_h = g_steal_pointer (&self->heights);
	old_w = g_steal_pointer (&self->weights);
	old_ids = g_steal_pointer (&self->ids);
	old_holes = g_steal_pointer (&self->holes);
	n_fl = lrg_foliage_get_n_layers (self->foliage);
	old_density = g_ptr_array_new_with_free_func (g_free);
	for (l = 0; l < n_fl; l++)
		g_ptr_array_add (old_density, g_memdup2 (_lrg_foliage_density (self->foliage, l), (gsize) ocx * ocz));

	self->chunks_x = chunks_x;
	self->chunks_z = chunks_z;
	self->nx = chunks_x * self->resolution + 1;
	self->nz = chunks_z * self->resolution + 1;
	alloc_grid (self);

	for (z = 0; z < self->nz; z++)
		for (x = 0; x < self->nx; x++)
		{
			guint sx = MIN (x, onx - 1), sz = MIN (z, onz - 1);
			gsize d = (gsize) z * self->nx + x, s = (gsize) sz * onx + sx;

			self->heights[d] = old_h[s];
			memcpy (self->weights + d * LRG_TERRAIN_WEIGHT_STRIDE, old_w + s * LRG_TERRAIN_WEIGHT_STRIDE,
			        LRG_TERRAIN_WEIGHT_STRIDE);
		}
	for (z = 0; z < MIN (ocz, self->nz - 1); z++)
		for (x = 0; x < MIN (ocx, self->nx - 1); x++)
		{
			gsize d = (gsize) z * (self->nx - 1) + x, s = (gsize) z * ocx + x;

			self->ids[d] = old_ids[s];
			self->holes[d] = old_holes[s];
			for (l = 0; l < n_fl; l++)
				_lrg_foliage_density (self->foliage, l)[d] = ((guint8 *) g_ptr_array_index (old_density, l))[s];
		}
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_HEIGHTS, 0, 0, 0, self->nx, self->nz);
}

gfloat
lrg_terrain_asset_get_height (LrgTerrainAsset *self,
                              guint            x,
                              guint            z)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	x = MIN (x, self->nx - 1);
	z = MIN (z, self->nz - 1);
	return self->heights[(gsize) z * self->nx + x];
}

void
lrg_terrain_asset_set_height (LrgTerrainAsset *self,
                              guint            x,
                              guint            z,
                              gfloat           height)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (x >= self->nx || z >= self->nz || !isfinite (height))
		return;
	self->heights[(gsize) z * self->nx + x] = height;
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_HEIGHTS, 0, x, z, 1, 1);
}

const gfloat *
lrg_terrain_asset_get_heights (LrgTerrainAsset *self,
                               guint           *n_heights)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	if (n_heights != NULL)
		*n_heights = self->nx * self->nz;
	return self->heights;
}

gboolean
lrg_terrain_asset_set_heights (LrgTerrainAsset  *self,
                               const gfloat     *heights,
                               guint             n_heights,
                               GError          **error)
{
	guint i;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	if (heights == NULL || n_heights != self->nx * self->nz)
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
		             "expected %u heights, got %u", self->nx * self->nz, n_heights);
		return FALSE;
	}
	for (i = 0; i < n_heights; i++)
		if (!isfinite (heights[i]))
		{
			g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
			             "height %u is not finite", i);
			return FALSE;
		}
	memcpy (self->heights, heights, sizeof (gfloat) * n_heights);
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_HEIGHTS, 0, 0, 0, self->nx, self->nz);
	return TRUE;
}

/* The sample-space position of local (x, z): fx in [0, nx - 1]. */
static gboolean
to_grid (LrgTerrainAsset *self,
         gfloat           x,
         gfloat           z,
         gfloat          *fx,
         gfloat          *fz)
{
	gfloat cell = self->chunk_size / (gfloat) self->resolution;

	*fx = x / cell;
	*fz = z / cell;
	return isfinite (*fx) && isfinite (*fz) && *fx >= 0.0f && *fz >= 0.0f &&
	       *fx <= (gfloat) (self->nx - 1) && *fz <= (gfloat) (self->nz - 1);
}

gboolean
lrg_terrain_asset_sample_height (LrgTerrainAsset *self,
                                 gfloat           x,
                                 gfloat           z,
                                 gfloat          *height)
{
	gfloat fx, fz, tx, tz;
	guint  ix, iz;
	gsize  i00;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	if (!to_grid (self, x, z, &fx, &fz))
		return FALSE;
	ix = MIN ((guint) fx, self->nx - 2);
	iz = MIN ((guint) fz, self->nz - 2);
	if (self->holes[(gsize) iz * (self->nx - 1) + ix] != 0)
		return FALSE;
	tx = fx - (gfloat) ix;
	tz = fz - (gfloat) iz;
	i00 = (gsize) iz * self->nx + ix;
	if (height != NULL)
		*height = (self->heights[i00] * (1.0f - tx) + self->heights[i00 + 1] * tx) * (1.0f - tz) +
		          (self->heights[i00 + self->nx] * (1.0f - tx) + self->heights[i00 + self->nx + 1] * tx) * tz;
	return TRUE;
}

/* The unit normal at sample (i, j) from central differences. */
static void
sample_normal_at (LrgTerrainAsset *self,
                  guint            i,
                  guint            j,
                  gfloat          *out)
{
	gfloat cell = self->chunk_size / (gfloat) self->resolution;
	guint  il = i > 0 ? i - 1 : i, ir = MIN (i + 1, self->nx - 1);
	guint  jd = j > 0 ? j - 1 : j, ju = MIN (j + 1, self->nz - 1);
	gfloat dx, dz, len;

	dx = (self->heights[(gsize) j * self->nx + ir] - self->heights[(gsize) j * self->nx + il]) /
	     ((gfloat) (ir - il) * cell);
	dz = (self->heights[(gsize) ju * self->nx + i] - self->heights[(gsize) jd * self->nx + i]) /
	     ((gfloat) (ju - jd) * cell);
	len = sqrtf (dx * dx + 1.0f + dz * dz);
	out[0] = -dx / len;
	out[1] = 1.0f / len;
	out[2] = -dz / len;
}

void
lrg_terrain_asset_sample_normal (LrgTerrainAsset *self,
                                 gfloat           x,
                                 gfloat           z,
                                 gfloat          *out)
{
	gfloat fx, fz, tx, tz, n[4][3], len;
	guint  ix, iz, k;

	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	g_return_if_fail (out != NULL);
	out[0] = 0.0f;
	out[1] = 1.0f;
	out[2] = 0.0f;
	if (!to_grid (self, x, z, &fx, &fz))
		return;
	ix = MIN ((guint) fx, self->nx - 2);
	iz = MIN ((guint) fz, self->nz - 2);
	tx = fx - (gfloat) ix;
	tz = fz - (gfloat) iz;
	sample_normal_at (self, ix, iz, n[0]);
	sample_normal_at (self, ix + 1, iz, n[1]);
	sample_normal_at (self, ix, iz + 1, n[2]);
	sample_normal_at (self, ix + 1, iz + 1, n[3]);
	for (k = 0; k < 3; k++)
		out[k] = (n[0][k] * (1.0f - tx) + n[1][k] * tx) * (1.0f - tz) + (n[2][k] * (1.0f - tx) + n[3][k] * tx) * tz;
	len = sqrtf (out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	if (len > 1e-6f)
	{
		out[0] /= len;
		out[1] /= len;
		out[2] /= len;
	}
}

void
lrg_terrain_asset_get_height_range (LrgTerrainAsset *self,
                                    gfloat          *min,
                                    gfloat          *max)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (min != NULL)
		*min = self->range_min;
	if (max != NULL)
		*max = self->range_max;
}

void
lrg_terrain_asset_set_height_range (LrgTerrainAsset *self,
                                    gfloat           min,
                                    gfloat           max)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (!isfinite (min) || !isfinite (max) || !(max > min))
		return;
	self->range_min = min;
	self->range_max = max;
}

LrgTerrainHeightFormat
lrg_terrain_asset_get_height_format (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), LRG_TERRAIN_HEIGHT_FORMAT_PNG16);
	return self->height_format;
}

void
lrg_terrain_asset_set_height_format (LrgTerrainAsset        *self,
                                     LrgTerrainHeightFormat  format)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (self->height_format != format)
	{
		self->height_format = format;
		g_clear_pointer (&self->height_file, g_free);   /* the extension changes */
	}
}

guint
lrg_terrain_asset_get_n_layers (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	return self->layers->len;
}

LrgTerrainLayer *
lrg_terrain_asset_get_layer (LrgTerrainAsset *self,
                             guint            index)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	if (index >= self->layers->len)
		return NULL;
	return g_ptr_array_index (self->layers, index);
}

LrgTerrainLayer *
lrg_terrain_asset_add_layer (LrgTerrainAsset *self,
                             const gchar     *name)
{
	LrgTerrainLayer *layer;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	if (self->layers->len >= LRG_TERRAIN_ASSET_MAX_LAYERS)
		return NULL;
	layer = lrg_terrain_layer_new (name);
	g_ptr_array_add (self->layers, layer);
	return layer;
}

gboolean
lrg_terrain_asset_remove_layer (LrgTerrainAsset *self,
                                guint            index)
{
	gsize n, i, cells;
	guint l;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	if (index >= self->layers->len || self->layers->len <= 1)
		return FALSE;
	g_ptr_array_remove_index (self->layers, index);

	/* shift the weight slots above @index down, renormalise; ids above
	 * it move down too (ids on it fall back to layer 0) */
	n = (gsize) self->nx * self->nz;
	for (i = 0; i < n; i++)
	{
		guint8 *w = self->weights + i * LRG_TERRAIN_WEIGHT_STRIDE;

		for (l = index; l + 1 < LRG_TERRAIN_WEIGHT_STRIDE; l++)
			w[l] = w[l + 1];
		w[LRG_TERRAIN_WEIGHT_STRIDE - 1] = 0;
		_lrg_terrain_normalize_weights (w, self->layers->len);
	}
	cells = (gsize) (self->nx - 1) * (self->nz - 1);
	for (i = 0; i < cells; i++)
	{
		if (self->ids[i] == index)
			self->ids[i] = 0;
		else if (self->ids[i] > index)
			self->ids[i]--;
	}
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_SPLAT, 0, 0, 0, self->nx, self->nz);
	return TRUE;
}

LrgTerrainSplatMode
lrg_terrain_asset_get_splat_mode (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), LRG_TERRAIN_SPLAT_MODE_WEIGHTS);
	return self->splat_mode;
}

void
lrg_terrain_asset_set_splat_mode (LrgTerrainAsset     *self,
                                  LrgTerrainSplatMode  mode)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (self->splat_mode == mode)
		return;
	self->splat_mode = mode;
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_SPLAT, 0, 0, 0, self->nx, self->nz);
}

gfloat
lrg_terrain_asset_get_weight (LrgTerrainAsset *self,
                              guint            x,
                              guint            z,
                              guint            layer)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	if (layer >= self->layers->len)
		return 0.0f;
	x = MIN (x, self->nx - 1);
	z = MIN (z, self->nz - 1);
	return (gfloat) self->weights[((gsize) z * self->nx + x) * LRG_TERRAIN_WEIGHT_STRIDE + layer] / 255.0f;
}

void
lrg_terrain_asset_set_weights (LrgTerrainAsset *self,
                               guint            x,
                               guint            z,
                               const gfloat    *weights,
                               guint            n_weights)
{
	guint8 *w;
	gfloat  sum = 0.0f;
	guint   l, n;

	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (x >= self->nx || z >= self->nz || weights == NULL)
		return;
	n = MIN (n_weights, self->layers->len);
	for (l = 0; l < n; l++)
		if (weights[l] > 0.0f && isfinite (weights[l]))
			sum += weights[l];
	w = self->weights + ((gsize) z * self->nx + x) * LRG_TERRAIN_WEIGHT_STRIDE;
	memset (w, 0, LRG_TERRAIN_WEIGHT_STRIDE);
	if (sum > 0.0f)
		for (l = 0; l < n; l++)
			if (weights[l] > 0.0f && isfinite (weights[l]))
				w[l] = (guint8) CLAMP (lrintf (weights[l] / sum * 255.0f), 0, 255);
	_lrg_terrain_normalize_weights (w, self->layers->len);
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_SPLAT, 0, x, z, 1, 1);
}

/* Bilinear layer weights at sample-space (fx, fz), into out[STRIDE]. */
static void
weights_at (LrgTerrainAsset *self,
            gfloat           fx,
            gfloat           fz,
            gfloat          *out)
{
	guint ix, iz, l;
	gfloat tx, tz;

	fx = CLAMP (fx, 0.0f, (gfloat) (self->nx - 1));
	fz = CLAMP (fz, 0.0f, (gfloat) (self->nz - 1));
	ix = MIN ((guint) fx, self->nx - 2);
	iz = MIN ((guint) fz, self->nz - 2);
	tx = fx - (gfloat) ix;
	tz = fz - (gfloat) iz;
	if (self->splat_mode == LRG_TERRAIN_SPLAT_MODE_IDS)
	{
		guint id = self->ids[(gsize) iz * (self->nx - 1) + ix];

		for (l = 0; l < LRG_TERRAIN_WEIGHT_STRIDE; l++)
			out[l] = 0.0f;
		out[MIN (id, self->layers->len - 1)] = 1.0f;
		return;
	}
	for (l = 0; l < LRG_TERRAIN_WEIGHT_STRIDE; l++)
	{
		const guint8 *w = self->weights + l;
		gsize         i00 = ((gsize) iz * self->nx + ix) * LRG_TERRAIN_WEIGHT_STRIDE;
		gsize         row = (gsize) self->nx * LRG_TERRAIN_WEIGHT_STRIDE;
		gfloat        a = w[i00], b = w[i00 + LRG_TERRAIN_WEIGHT_STRIDE];
		gfloat        c = w[i00 + row], d = w[i00 + row + LRG_TERRAIN_WEIGHT_STRIDE];

		out[l] = ((a * (1.0f - tx) + b * tx) * (1.0f - tz) + (c * (1.0f - tx) + d * tx) * tz) / 255.0f;
	}
}

void
lrg_terrain_asset_sample_weights (LrgTerrainAsset *self,
                                  gfloat           x,
                                  gfloat           z,
                                  gfloat          *out)
{
	gfloat cell;

	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	g_return_if_fail (out != NULL);
	cell = self->chunk_size / (gfloat) self->resolution;
	weights_at (self, x / cell, z / cell, out);
}

guint
lrg_terrain_asset_get_cell_material (LrgTerrainAsset *self,
                                     guint            x,
                                     guint            z)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	x = MIN (x, self->nx - 2);
	z = MIN (z, self->nz - 2);
	return self->ids[(gsize) z * (self->nx - 1) + x];
}

void
lrg_terrain_asset_set_cell_material (LrgTerrainAsset *self,
                                     guint            x,
                                     guint            z,
                                     guint            layer)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (x >= self->nx - 1 || z >= self->nz - 1)
		return;
	self->ids[(gsize) z * (self->nx - 1) + x] = (guint8) MIN (layer, self->layers->len - 1);
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_MATERIAL_IDS, 0, x, z, 1, 1);
}

gboolean
lrg_terrain_asset_get_hole (LrgTerrainAsset *self,
                            guint            x,
                            guint            z)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	if (x >= self->nx - 1 || z >= self->nz - 1)
		return FALSE;
	return self->holes[(gsize) z * (self->nx - 1) + x] != 0;
}

void
lrg_terrain_asset_set_hole (LrgTerrainAsset *self,
                            guint            x,
                            guint            z,
                            gboolean         hole)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (x >= self->nx - 1 || z >= self->nz - 1)
		return;
	self->holes[(gsize) z * (self->nx - 1) + x] = hole ? 1 : 0;
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_HOLES, 0, x, z, 1, 1);
}

guint
lrg_terrain_asset_get_hole_count (LrgTerrainAsset *self)
{
	gsize i, cells;
	guint n = 0;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	cells = (gsize) (self->nx - 1) * (self->nz - 1);
	for (i = 0; i < cells; i++)
		if (self->holes[i] != 0)
			n++;
	return n;
}

guint
lrg_terrain_asset_get_lod_levels (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 1);
	return self->lod_levels;
}

void
lrg_terrain_asset_set_lod_levels (LrgTerrainAsset *self,
                                  guint            levels)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	self->lod_levels = CLAMP (levels, 1u, (guint) LRG_TERRAIN_ASSET_MAX_LOD);
}

gfloat
lrg_terrain_asset_get_lod_distance (LrgTerrainAsset *self,
                                    guint            level)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	if (level == 0 || level >= self->lod_levels)
		return 0.0f;
	return self->lod_distances[level];
}

void
lrg_terrain_asset_set_lod_distance (LrgTerrainAsset *self,
                                    guint            level,
                                    gfloat           distance)
{
	guint l;

	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (level == 0 || level >= LRG_TERRAIN_ASSET_MAX_LOD || !(distance > 0.0f) || !isfinite (distance))
		return;
	self->lod_distances[level] = distance;
	for (l = level + 1; l < LRG_TERRAIN_ASSET_MAX_LOD; l++)
		if (self->lod_distances[l] <= self->lod_distances[l - 1])
			self->lod_distances[l] = self->lod_distances[l - 1] * 2.0f;
	for (l = level; l > 1; l--)
		if (self->lod_distances[l - 1] >= self->lod_distances[l])
			self->lod_distances[l - 1] = self->lod_distances[l] * 0.5f;
}

guint
lrg_terrain_asset_select_lod (LrgTerrainAsset *self,
                              gfloat           distance)
{
	guint l, level = 0;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	for (l = 1; l < self->lod_levels; l++)
		if (distance >= self->lod_distances[l])
			level = l;
	return level;
}

gfloat
lrg_terrain_asset_get_skirt_depth (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0.0f);
	return self->skirt_depth;
}

void
lrg_terrain_asset_set_skirt_depth (LrgTerrainAsset *self,
                                   gfloat           depth)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	if (depth >= 0.0f && isfinite (depth))
		self->skirt_depth = depth;
}

guint
lrg_terrain_asset_get_bake_resolution (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 256);
	return self->bake_resolution;
}

void
lrg_terrain_asset_set_bake_resolution (LrgTerrainAsset *self,
                                       guint            texels)
{
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (self));
	texels = CLAMP (texels, 16u, 2048u);
	if (self->bake_resolution == texels)
		return;
	self->bake_resolution = texels;
	_lrg_terrain_asset_emit_changed (self, LRG_TERRAIN_EDIT_SPLAT, 0, 0, 0, self->nx, self->nz);
}

LrgFoliage *
lrg_terrain_asset_get_foliage (LrgTerrainAsset *self)
{
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	return self->foliage;
}

/* ==========================================================================
 * Chunk meshes
 * ========================================================================== */

void
_lrg_terrain_chunk_mesh_clear (TerrainChunkMesh *mesh)
{
	g_clear_pointer (&mesh->positions, g_free);
	g_clear_pointer (&mesh->normals, g_free);
	g_clear_pointer (&mesh->texcoords, g_free);
	g_clear_pointer (&mesh->indices, g_free);
	mesh->n_vertices = mesh->n_indices = 0;
}

/* The finest step <= 2^lod that divides the resolution. */
static guint
lod_step (LrgTerrainAsset *self,
          guint            lod)
{
	guint step = 1u << MIN (lod, (guint) LRG_TERRAIN_ASSET_MAX_LOD - 1);

	while (step > 1 && self->resolution % step != 0)
		step >>= 1;
	return step;
}

/* Whether any cell of the step x step block at cell (ci, cj) is a hole. */
static gboolean
block_has_hole (LrgTerrainAsset *self,
                guint            ci,
                guint            cj,
                guint            step)
{
	guint a, b;

	for (b = 0; b < step; b++)
		for (a = 0; a < step; a++)
			if (self->holes[(gsize) (cj + b) * (self->nx - 1) + ci + a] != 0)
				return TRUE;
	return FALSE;
}

gboolean
_lrg_terrain_asset_build_chunk (LrgTerrainAsset  *self,
                                guint             cx,
                                guint             cz,
                                guint             lod,
                                TerrainChunkMesh *out)
{
	gfloat cell = self->chunk_size / (gfloat) self->resolution;
	guint  step, n, side, a, b, i0, j0, s, k, v, t;
	guint  n_grid, n_skirt, n_tris_max;

	memset (out, 0, sizeof *out);
	if (cx >= self->chunks_x || cz >= self->chunks_z)
		return FALSE;

	step = lod_step (self, lod);
	n = self->resolution / step;
	side = n + 1;
	i0 = cx * self->resolution;
	j0 = cz * self->resolution;
	n_grid = side * side;
	n_skirt = self->skirt_depth > 0.0f ? 4 * side : 0;
	out->n_vertices = n_grid + n_skirt;
	n_tris_max = n * n * 2 + (n_skirt > 0 ? 4 * n * 4 : 0);
	out->positions = g_new (gfloat, (gsize) out->n_vertices * 3);
	out->normals = g_new (gfloat, (gsize) out->n_vertices * 3);
	out->texcoords = g_new (gfloat, (gsize) out->n_vertices * 2);
	out->indices = g_new (guint16, (gsize) n_tris_max * 3);
	out->min[0] = out->min[1] = out->min[2] = G_MAXFLOAT;
	out->max[0] = out->max[1] = out->max[2] = -G_MAXFLOAT;

	/* 1. The grid: one vertex per used sample, normals from the whole
	 *    grid (no seams between chunks), texcoords across the chunk */
	for (b = 0; b < side; b++)
		for (a = 0; a < side; a++)
		{
			guint  i = i0 + a * step, j = j0 + b * step;
			gfloat *p = out->positions + (gsize) (b * side + a) * 3;

			p[0] = (gfloat) i * cell;
			p[1] = self->heights[(gsize) j * self->nx + i];
			p[2] = (gfloat) j * cell;
			sample_normal_at (self, i, j, out->normals + (gsize) (b * side + a) * 3);
			out->texcoords[(gsize) (b * side + a) * 2] = (gfloat) a / (gfloat) n;
			out->texcoords[(gsize) (b * side + a) * 2 + 1] = (gfloat) b / (gfloat) n;
			for (k = 0; k < 3; k++)
			{
				out->min[k] = MIN (out->min[k], p[k]);
				out->max[k] = MAX (out->max[k], p[k]);
			}
		}

	/* 2. Two counter-clockwise (seen from +Y) triangles per block that
	 *    holds no hole, split along (i, j) - (i + 1, j + 1) like the
	 *    physics heightfield */
	t = 0;
	for (b = 0; b < n; b++)
		for (a = 0; a < n; a++)
		{
			guint16 v00 = (guint16) (b * side + a), v10 = (guint16) (v00 + 1);
			guint16 v01 = (guint16) (v00 + side), v11 = (guint16) (v01 + 1);

			if (block_has_hole (self, i0 + a * step, j0 + b * step, step))
				continue;
			out->indices[t++] = v00;
			out->indices[t++] = v01;
			out->indices[t++] = v11;
			out->indices[t++] = v00;
			out->indices[t++] = v11;
			out->indices[t++] = v10;
		}

	/* 3. Skirts: each border vertex again, skirt_depth lower; a quad per
	 *    border segment, both windings so it shows from either side */
	if (n_skirt > 0)
	{
		for (s = 0; s < 4; s++)
		{
			for (k = 0; k < side; k++)
			{
				guint src;

				switch (s)
				{
				case 0:
					src = k;                         /* -Z border */
					break;
				case 1:
					src = (side - 1) * side + k;     /* +Z border */
					break;
				case 2:
					src = k * side;                  /* -X border */
					break;
				default:
					src = k * side + side - 1;       /* +X border */
					break;
				}
				v = n_grid + s * side + k;
				memcpy (out->positions + (gsize) v * 3, out->positions + (gsize) src * 3, sizeof (gfloat) * 3);
				out->positions[(gsize) v * 3 + 1] -= self->skirt_depth;
				memcpy (out->normals + (gsize) v * 3, out->normals + (gsize) src * 3, sizeof (gfloat) * 3);
				memcpy (out->texcoords + (gsize) v * 2, out->texcoords + (gsize) src * 2, sizeof (gfloat) * 2);
				out->min[1] = MIN (out->min[1], out->positions[(gsize) v * 3 + 1]);
				if (k > 0)
				{
					guint16 top0, top1, bot0 = (guint16) (v - 1), bot1 = (guint16) v;

					switch (s)
					{
					case 0:
						top0 = (guint16) (k - 1);
						top1 = (guint16) k;
						break;
					case 1:
						top0 = (guint16) ((side - 1) * side + k - 1);
						top1 = (guint16) ((side - 1) * side + k);
						break;
					case 2:
						top0 = (guint16) ((k - 1) * side);
						top1 = (guint16) (k * side);
						break;
					default:
						top0 = (guint16) ((k - 1) * side + side - 1);
						top1 = (guint16) (k * side + side - 1);
						break;
					}
					out->indices[t++] = top0;
					out->indices[t++] = bot0;
					out->indices[t++] = bot1;
					out->indices[t++] = top0;
					out->indices[t++] = bot1;
					out->indices[t++] = top1;
					out->indices[t++] = top0;
					out->indices[t++] = bot1;
					out->indices[t++] = bot0;
					out->indices[t++] = top0;
					out->indices[t++] = top1;
					out->indices[t++] = bot1;
				}
			}
		}
	}
	out->n_indices = t;
	return TRUE;
}

gboolean
lrg_terrain_asset_get_chunk_mesh_counts (LrgTerrainAsset *self,
                                         guint            cx,
                                         guint            cz,
                                         guint            lod,
                                         guint           *n_vertices,
                                         guint           *n_triangles)
{
	TerrainChunkMesh mesh;
	gboolean         ok;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	ok = _lrg_terrain_asset_build_chunk (self, cx, cz, lod, &mesh);
	if (n_vertices != NULL)
		*n_vertices = ok ? mesh.n_vertices : 0;
	if (n_triangles != NULL)
		*n_triangles = ok ? mesh.n_indices / 3 : 0;
	_lrg_terrain_chunk_mesh_clear (&mesh);
	return ok;
}

/* ==========================================================================
 * Splat bake
 * ========================================================================== */

gboolean
lrg_terrain_asset_bake_chunk (LrgTerrainAsset *self,
                              guint            cx,
                              guint            cz,
                              guint8          *out_rgba)
{
	gfloat cell, scale;
	guint  B, u, v, l, n_layers;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	g_return_val_if_fail (out_rgba != NULL, FALSE);
	if (cx >= self->chunks_x || cz >= self->chunks_z)
		return FALSE;

	n_layers = self->layers->len;
	for (l = 0; l < n_layers; l++)
		layer_ensure_pixels (g_ptr_array_index (self->layers, l), self->base_dir);

	cell = self->chunk_size / (gfloat) self->resolution;
	B = self->bake_resolution;
	scale = (gfloat) self->resolution / (gfloat) B;
	for (v = 0; v < B; v++)
		for (u = 0; u < B; u++)
		{
			gfloat  fx = (gfloat) (cx * self->resolution) + ((gfloat) u + 0.5f) * scale;
			gfloat  fz = (gfloat) (cz * self->resolution) + ((gfloat) v + 0.5f) * scale;
			gfloat  w[LRG_TERRAIN_WEIGHT_STRIDE], c[3] = { 0.0f, 0.0f, 0.0f };
			guint8 *o = out_rgba + ((gsize) v * B + u) * 4;

			weights_at (self, fx, fz, w);
			for (l = 0; l < n_layers; l++)
			{
				LrgTerrainLayer *layer;
				gfloat           t[3];

				if (w[l] <= 0.0f)
					continue;
				layer = g_ptr_array_index (self->layers, l);
				layer_sample (layer, fx * cell / layer->tiling, fz * cell / layer->tiling, t);
				c[0] += w[l] * t[0] * layer->color[0];
				c[1] += w[l] * t[1] * layer->color[1];
				c[2] += w[l] * t[2] * layer->color[2];
			}
			o[0] = (guint8) CLAMP (lrintf (c[0] * 255.0f), 0, 255);
			o[1] = (guint8) CLAMP (lrintf (c[1] * 255.0f), 0, 255);
			o[2] = (guint8) CLAMP (lrintf (c[2] * 255.0f), 0, 255);
			o[3] = 255;
		}
	return TRUE;
}

/* ==========================================================================
 * Physics and picking
 * ========================================================================== */

LrgPhysicsShape *
lrg_terrain_asset_build_physics_shape (LrgTerrainAsset  *self,
                                       gfloat            scale_x,
                                       gfloat            scale_y,
                                       gfloat            scale_z,
                                       GError          **error)
{
	g_autofree gfloat *h = NULL;
	gfloat             cell;
	guint              x, z;
	guint              cxn, czn;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	if (!(scale_x > 0.0f) || !(scale_y > 0.0f) || !(scale_z > 0.0f))
	{
		g_set_error_literal (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_INVALID,
		                     "physics scales must be positive");
		return NULL;
	}
	cell = self->chunk_size / (gfloat) self->resolution;
	cxn = self->nx - 1;
	czn = self->nz - 1;
	h = g_memdup2 (self->heights, sizeof (gfloat) * self->nx * self->nz);

	/* A sample is a hole when every cell around it is one */
	for (z = 0; z < self->nz; z++)
		for (x = 0; x < self->nx; x++)
		{
			gboolean all = TRUE, any = FALSE;
			gint     dx, dz;

			for (dz = -1; dz <= 0; dz++)
				for (dx = -1; dx <= 0; dx++)
				{
					gint ci = (gint) x + dx, cj = (gint) z + dz;

					if (ci < 0 || cj < 0 || ci >= (gint) cxn || cj >= (gint) czn)
						continue;
					any = TRUE;
					if (self->holes[(gsize) cj * cxn + (gsize) ci] == 0)
						all = FALSE;
				}
			if (any && all)
				h[(gsize) z * self->nx + x] = LRG_PHYSICS_HEIGHTFIELD_HOLE;
		}
	return lrg_physics_shape_new_heightfield (h, self->nx * self->nz, self->nx, self->nz, cell * scale_x,
	                                          scale_y, cell * scale_z, error);
}

/* Height of the ray above the ground at parameter t; FALSE off the grid
 * or over a hole. */
static gboolean
ray_gap (LrgTerrainAsset *self,
         const gfloat    *o,
         const gfloat    *d,
         gfloat           t,
         gfloat          *gap)
{
	gfloat h;

	if (!lrg_terrain_asset_sample_height (self, o[0] + d[0] * t, o[2] + d[2] * t, &h))
		return FALSE;
	*gap = o[1] + d[1] * t - h;
	return TRUE;
}

gboolean
lrg_terrain_asset_raycast (LrgTerrainAsset *self,
                           const gfloat    *origin,
                           const gfloat    *direction,
                           gfloat           max_distance,
                           gfloat          *out_point)
{
	gfloat   len, dt, t, prev_t = 0.0f, prev_gap = 0.0f, gap;
	gboolean have_prev = FALSE;
	guint    it;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	g_return_val_if_fail (origin != NULL && direction != NULL, FALSE);
	len = sqrtf (direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
	if (!(len > 0.0f) || !(max_distance > 0.0f))
		return FALSE;

	/* march in half-cell steps; on a crossing (above -> at or below),
	 * bisect between the two parameters */
	dt = 0.5f * (self->chunk_size / (gfloat) self->resolution) / len;
	for (t = 0.0f; t <= max_distance + dt; t += dt)
	{
		gfloat tt = MIN (t, max_distance);

		if (!ray_gap (self, origin, direction, tt, &gap))
		{
			have_prev = FALSE;
			continue;
		}
		if (gap <= 0.0f)
		{
			gfloat lo = have_prev && prev_gap > 0.0f ? prev_t : tt, hi = tt;

			for (it = 0; it < 32 && lo < hi; it++)
			{
				gfloat mid = 0.5f * (lo + hi), g;

				if (ray_gap (self, origin, direction, mid, &g) && g > 0.0f)
					lo = mid;
				else
					hi = mid;
			}
			if (out_point != NULL)
			{
				out_point[0] = origin[0] + direction[0] * hi;
				out_point[1] = origin[1] + direction[1] * hi;
				out_point[2] = origin[2] + direction[2] * hi;
				/* sit exactly on the surface */
				lrg_terrain_asset_sample_height (self, out_point[0], out_point[2], &out_point[1]);
			}
			return TRUE;
		}
		prev_t = tt;
		prev_gap = gap;
		have_prev = TRUE;
		if (tt >= max_distance)
			break;
	}
	return FALSE;
}

/* ==========================================================================
 * Edits
 * ========================================================================== */

/* The grid an edit kind is addressed in, and its element size. */
static gboolean
edit_layout (LrgTerrainAsset    *self,
             LrgTerrainEditKind  kind,
             guint               layer,
             guint              *lim_x,
             guint              *lim_z,
             gsize              *elem,
             guint8            **data)
{
	switch (kind)
	{
	case LRG_TERRAIN_EDIT_HEIGHTS:
		*lim_x = self->nx;
		*lim_z = self->nz;
		*elem = sizeof (gfloat);
		*data = (guint8 *) self->heights;
		return TRUE;
	case LRG_TERRAIN_EDIT_SPLAT:
		*lim_x = self->nx;
		*lim_z = self->nz;
		*elem = LRG_TERRAIN_WEIGHT_STRIDE;
		*data = self->weights;
		return TRUE;
	case LRG_TERRAIN_EDIT_MATERIAL_IDS:
		*lim_x = self->nx - 1;
		*lim_z = self->nz - 1;
		*elem = 1;
		*data = self->ids;
		return TRUE;
	case LRG_TERRAIN_EDIT_HOLES:
		*lim_x = self->nx - 1;
		*lim_z = self->nz - 1;
		*elem = 1;
		*data = self->holes;
		return TRUE;
	case LRG_TERRAIN_EDIT_FOLIAGE:
		*lim_x = self->nx - 1;
		*lim_z = self->nz - 1;
		*elem = 1;
		*data = _lrg_foliage_density (self->foliage, layer);
		return *data != NULL;
	default:
		return FALSE;
	}
}

/* Copies a region out of the grid. */
static GBytes *
copy_region (const guint8 *data,
             guint         lim_x,
             gsize         elem,
             guint         x,
             guint         z,
             guint         w,
             guint         h)
{
	guint8 *buf = g_malloc ((gsize) w * h * elem);
	guint   r;

	for (r = 0; r < h; r++)
		memcpy (buf + (gsize) r * w * elem, data + ((gsize) (z + r) * lim_x + x) * elem, (gsize) w * elem);
	return g_bytes_new_take (buf, (gsize) w * h * elem);
}

LrgTerrainEdit *
lrg_terrain_asset_capture_edit (LrgTerrainAsset    *self,
                                LrgTerrainEditKind  kind,
                                guint               layer,
                                guint               x,
                                guint               z,
                                guint               width,
                                guint               height)
{
	LrgTerrainEdit *edit;
	guint           lim_x, lim_z;
	gsize           elem;
	guint8         *data;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	if (!edit_layout (self, kind, layer, &lim_x, &lim_z, &elem, &data))
		return NULL;
	if (x >= lim_x || z >= lim_z)
		return NULL;
	width = MIN (width, lim_x - x);
	height = MIN (height, lim_z - z);
	if (width == 0 || height == 0)
		return NULL;
	edit = g_new0 (LrgTerrainEdit, 1);
	edit->kind = kind;
	edit->layer = kind == LRG_TERRAIN_EDIT_FOLIAGE ? layer : 0;
	edit->x = x;
	edit->z = z;
	edit->width = width;
	edit->height = height;
	edit->before = copy_region (data, lim_x, elem, x, z, width, height);
	edit->after = g_bytes_ref (edit->before);
	return edit;
}

void
lrg_terrain_edit_capture_after (LrgTerrainEdit  *edit,
                                LrgTerrainAsset *asset)
{
	guint   lim_x, lim_z;
	gsize   elem;
	guint8 *data;

	g_return_if_fail (edit != NULL);
	g_return_if_fail (LRG_IS_TERRAIN_ASSET (asset));
	if (!edit_layout (asset, edit->kind, edit->layer, &lim_x, &lim_z, &elem, &data))
		return;
	if (edit->x + edit->width > lim_x || edit->z + edit->height > lim_z)
		return;
	g_clear_pointer (&edit->after, g_bytes_unref);
	edit->after = copy_region (data, lim_x, elem, edit->x, edit->z, edit->width, edit->height);
}

gboolean
lrg_terrain_edit_apply (const LrgTerrainEdit *edit,
                        LrgTerrainAsset      *asset,
                        gboolean              after)
{
	const guint8 *src;
	gsize         len;
	guint         lim_x, lim_z, r;
	gsize         elem;
	guint8       *data;

	g_return_val_if_fail (edit != NULL, FALSE);
	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (asset), FALSE);
	if (!edit_layout (asset, edit->kind, edit->layer, &lim_x, &lim_z, &elem, &data))
		return FALSE;
	if (edit->x + edit->width > lim_x || edit->z + edit->height > lim_z)
		return FALSE;
	src = g_bytes_get_data (after ? edit->after : edit->before, &len);
	if (len != (gsize) edit->width * edit->height * elem)
		return FALSE;
	for (r = 0; r < edit->height; r++)
		memcpy (data + ((gsize) (edit->z + r) * lim_x + edit->x) * elem, src + (gsize) r * edit->width * elem,
		        (gsize) edit->width * elem);
	_lrg_terrain_asset_emit_changed (asset, edit->kind, edit->layer, edit->x, edit->z, edit->width, edit->height);
	return TRUE;
}

/* ==========================================================================
 * Foliage scatter entry point
 * ========================================================================== */

guint
lrg_terrain_asset_scatter_foliage (LrgTerrainAsset *self)
{
	guint l, total = 0;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), 0);
	lrg_foliage_mark_dirty (self->foliage, 0, 0, self->nx - 1, self->nz - 1);
	_lrg_foliage_scatter (self->foliage, self);
	for (l = 0; l < lrg_foliage_get_n_layers (self->foliage); l++)
		total += lrg_foliage_get_instance_count (self->foliage, l);
	return total;
}

/* ==========================================================================
 * Loading
 * ========================================================================== */

/* Resolves a sidecar name against the base directory. */
static gchar *
sidecar_path (const gchar *base_dir,
              const gchar *name)
{
	if (g_path_is_absolute (name))
		return g_strdup (name);
	return g_build_filename (base_dir, name, NULL);
}

/* Reads a PNG sidecar and checks its size. */
static gboolean
load_sidecar_png (TerrainReader *rd,
                  const gchar   *base_dir,
                  const gchar   *name,
                  const gchar   *path,
                  guint          width,
                  guint          height,
                  TerrainImage  *img)
{
	g_autofree gchar *file = sidecar_path (base_dir, name);
	g_autoptr(GError) local = NULL;

	if (!_lrg_terrain_png_load (file, img, &local))
	{
		rd->code = local->code;
		return _lrg_terrain_rd_fail (rd, path, "%s: %s", name, local->message);
	}
	if (img->width != width || img->height != height)
	{
		guint got_w = img->width, got_h = img->height;

		_lrg_terrain_image_clear (img);
		rd->code = LRG_TERRAIN_ASSET_ERROR_INVALID;
		return _lrg_terrain_rd_fail (rd, path, "%s: expected a %ux%u image, got %ux%u", name, width, height,
		                             got_w, got_h);
	}
	return TRUE;
}

/* The heights sidecar: PNG16 over the range, or raw little-endian floats. */
static gboolean
load_heights (LrgTerrainAsset *self,
              TerrainReader   *rd,
              const gchar     *base_dir)
{
	gsize n = (gsize) self->nx * self->nz, i;

	if (self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_R32)
	{
		g_autofree gchar *file = sidecar_path (base_dir, self->height_file);
		g_autofree gchar *contents = NULL;
		g_autoptr(GError) local = NULL;
		gsize             len = 0;

		if (!g_file_get_contents (file, &contents, &len, &local))
		{
			rd->code = LRG_TERRAIN_ASSET_ERROR_IO;
			return _lrg_terrain_rd_fail (rd, "/heights/file", "%s", local->message);
		}
		if (len != n * 4)
		{
			rd->code = LRG_TERRAIN_ASSET_ERROR_INVALID;
			return _lrg_terrain_rd_fail (rd, "/heights/file", "%s: expected %" G_GSIZE_FORMAT
			                             " bytes (%ux%u floats), got %" G_GSIZE_FORMAT,
			                             self->height_file, n * 4, self->nx, self->nz, len);
		}
		for (i = 0; i < n; i++)
		{
			guint32 bits;
			gfloat  f;

			memcpy (&bits, contents + i * 4, 4);
			bits = GUINT32_FROM_LE (bits);
			memcpy (&f, &bits, 4);
			if (!isfinite (f))
			{
				rd->code = LRG_TERRAIN_ASSET_ERROR_INVALID;
				return _lrg_terrain_rd_fail (rd, "/heights/file", "%s: height %" G_GSIZE_FORMAT " is not finite",
				                             self->height_file, i);
			}
			self->heights[i] = f;
		}
	}
	else
	{
		TerrainImage img;
		gdouble      span = (gdouble) self->range_max - (gdouble) self->range_min;

		if (!load_sidecar_png (rd, base_dir, self->height_file, "/heights/file", self->nx, self->nz, &img))
			return FALSE;
		for (i = 0; i < n; i++)
			self->heights[i] = (gfloat) ((gdouble) self->range_min +
			                             (gdouble) _lrg_terrain_image_get16 (&img, (guint) (i % self->nx),
			                                                                 (guint) (i / self->nx), 0) *
			                             span / 65535.0);
		_lrg_terrain_image_clear (&img);
	}
	return TRUE;
}

/* Splat weights (RGBA PNGs of four layers each), ids and holes. */
static gboolean
load_surface_sidecars (LrgTerrainAsset *self,
                       TerrainReader   *rd,
                       const gchar     *base_dir)
{
	gsize n = (gsize) self->nx * self->nz, cells = (gsize) (self->nx - 1) * (self->nz - 1), i;
	guint f, c;

	/* weights: start empty when any file is given, fill, normalise */
	if (self->splat_files[0] != NULL)
	{
		memset (self->weights, 0, n * LRG_TERRAIN_WEIGHT_STRIDE);
		for (f = 0; f < 2; f++)
		{
			TerrainImage      img;
			g_autofree gchar *p = g_strdup_printf ("/splat/files[%u]", f);

			if (self->splat_files[f] == NULL)
				continue;
			if (!load_sidecar_png (rd, base_dir, self->splat_files[f], p, self->nx, self->nz, &img))
				return FALSE;
			for (i = 0; i < n; i++)
				for (c = 0; c < 4; c++)
				{
					guint16 v = c < img.channels ? _lrg_terrain_image_get16 (&img, (guint) (i % self->nx),
					                                                         (guint) (i / self->nx), c)
					                             : 0;

					self->weights[i * LRG_TERRAIN_WEIGHT_STRIDE + f * 4 + c] = (guint8) ((v + 128u) / 257u);
				}
			_lrg_terrain_image_clear (&img);
		}
		for (i = 0; i < n; i++)
			_lrg_terrain_normalize_weights (self->weights + i * LRG_TERRAIN_WEIGHT_STRIDE, self->layers->len);
	}

	if (self->ids_file != NULL)
	{
		TerrainImage img;

		if (!load_sidecar_png (rd, base_dir, self->ids_file, "/splat/ids_file", self->nx - 1, self->nz - 1, &img))
			return FALSE;
		for (i = 0; i < cells; i++)
		{
			guint id = (_lrg_terrain_image_get16 (&img, (guint) (i % (self->nx - 1)), (guint) (i / (self->nx - 1)),
			                                      0) + 128u) / 257u;

			self->ids[i] = (guint8) MIN (id, self->layers->len - 1);
		}
		_lrg_terrain_image_clear (&img);
	}

	if (self->holes_file != NULL)
	{
		TerrainImage img;

		if (!load_sidecar_png (rd, base_dir, self->holes_file, "/holes/file", self->nx - 1, self->nz - 1, &img))
			return FALSE;
		for (i = 0; i < cells; i++)
			self->holes[i] = _lrg_terrain_image_get16 (&img, (guint) (i % (self->nx - 1)),
			                                           (guint) (i / (self->nx - 1)), 0) >= 32768 ? 1 : 0;
		_lrg_terrain_image_clear (&img);
	}
	return TRUE;
}

static gboolean
read_grid (LrgTerrainAsset *self,
           TerrainReader   *rd,
           YamlNode        *node)
{
	YamlMapping *map;
	guint        resolution = 0, i;
	gfloat       chunk_size = 0.0f, chunks[2] = { 0.0f, 0.0f };
	gboolean     have_chunks = FALSE;

	if (!_lrg_terrain_rd_mapping (rd, node, "/grid", &map))
		return FALSE;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *member = yaml_mapping_get_value (map, i);

		if (g_strcmp0 (key, "resolution") == 0)
		{
			if (!_lrg_terrain_rd_uint (rd, member, "/grid/resolution", 1, LRG_TERRAIN_ASSET_MAX_RESOLUTION,
			                           &resolution))
				return FALSE;
		}
		else if (g_strcmp0 (key, "chunk_size") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, "/grid/chunk_size", 0.001, 1.0e6, &chunk_size))
				return FALSE;
		}
		else if (g_strcmp0 (key, "chunks") == 0)
		{
			if (!_lrg_terrain_rd_numbers (rd, member, "/grid/chunks", 2, 2, chunks, NULL))
				return FALSE;
			if (chunks[0] < 1.0f || chunks[1] < 1.0f || chunks[0] > (gfloat) LRG_TERRAIN_ASSET_MAX_CHUNKS ||
			    chunks[1] > (gfloat) LRG_TERRAIN_ASSET_MAX_CHUNKS || chunks[0] != floorf (chunks[0]) ||
			    chunks[1] != floorf (chunks[1]))
				return _lrg_terrain_rd_fail (rd, "/grid/chunks", "expected two whole numbers from 1 to %u",
				                             (guint) LRG_TERRAIN_ASSET_MAX_CHUNKS);
			have_chunks = TRUE;
		}
		else
			_lrg_terrain_keep_extra (&self->extra_grid, key, member);
	}
	if (resolution == 0)
		return _lrg_terrain_rd_fail (rd, "/grid/resolution", "missing");
	if (chunk_size <= 0.0f)
		return _lrg_terrain_rd_fail (rd, "/grid/chunk_size", "missing");
	if (!have_chunks)
		return _lrg_terrain_rd_fail (rd, "/grid/chunks", "missing");
	set_grid (self, resolution, chunk_size, (guint) chunks[0], (guint) chunks[1]);
	return TRUE;
}

static gboolean
read_heights (LrgTerrainAsset *self,
              TerrainReader   *rd,
              YamlNode        *node)
{
	YamlMapping *map;
	guint        i;
	gint         fmt;

	if (!_lrg_terrain_rd_mapping (rd, node, "/heights", &map))
		return FALSE;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *member = yaml_mapping_get_value (map, i);

		if (g_strcmp0 (key, "file") == 0)
		{
			const gchar *s;

			if (!_lrg_terrain_rd_string (rd, member, "/heights/file", &s))
				return FALSE;
			g_free (self->height_file);
			self->height_file = g_strdup (s);
		}
		else if (g_strcmp0 (key, "format") == 0)
		{
			if (!_lrg_terrain_rd_enum (rd, member, "/heights/format", LRG_TYPE_TERRAIN_HEIGHT_FORMAT, &fmt))
				return FALSE;
			self->height_format = (LrgTerrainHeightFormat) fmt;
		}
		else if (g_strcmp0 (key, "range") == 0)
		{
			gfloat r[2];

			if (!_lrg_terrain_rd_numbers (rd, member, "/heights/range", 2, 2, r, NULL))
				return FALSE;
			if (!(r[1] > r[0]))
				return _lrg_terrain_rd_fail (rd, "/heights/range", "the maximum must be above the minimum");
			self->range_min = r[0];
			self->range_max = r[1];
		}
		else
			_lrg_terrain_keep_extra (&self->extra_heights, key, member);
	}
	return TRUE;
}

static gboolean
read_splat (LrgTerrainAsset *self,
            TerrainReader   *rd,
            YamlNode        *node)
{
	YamlMapping *map;
	guint        i, j;
	gint         mode;

	if (!_lrg_terrain_rd_mapping (rd, node, "/splat", &map))
		return FALSE;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *member = yaml_mapping_get_value (map, i);

		if (g_strcmp0 (key, "mode") == 0)
		{
			if (!_lrg_terrain_rd_enum (rd, member, "/splat/mode", LRG_TYPE_TERRAIN_SPLAT_MODE, &mode))
				return FALSE;
			self->splat_mode = (LrgTerrainSplatMode) mode;
		}
		else if (g_strcmp0 (key, "files") == 0)
		{
			YamlSequence *seq;

			if (!_lrg_terrain_rd_sequence (rd, member, "/splat/files", &seq))
				return FALSE;
			if (yaml_sequence_get_length (seq) > 2)
				return _lrg_terrain_rd_fail (rd, "/splat/files", "at most two files (eight layers)");
			for (j = 0; j < yaml_sequence_get_length (seq); j++)
			{
				g_autofree gchar *p = g_strdup_printf ("/splat/files[%u]", j);
				const gchar      *s;

				if (!_lrg_terrain_rd_string (rd, yaml_sequence_get_element (seq, j), p, &s))
					return FALSE;
				g_free (self->splat_files[j]);
				self->splat_files[j] = g_strdup (s);
			}
		}
		else if (g_strcmp0 (key, "ids_file") == 0)
		{
			const gchar *s;

			if (!_lrg_terrain_rd_string (rd, member, "/splat/ids_file", &s))
				return FALSE;
			g_free (self->ids_file);
			self->ids_file = g_strdup (s);
		}
		else
			_lrg_terrain_keep_extra (&self->extra_splat, key, member);
	}
	return TRUE;
}

static gboolean
read_layer (LrgTerrainAsset *self,
            TerrainReader   *rd,
            YamlNode        *node,
            guint            index)
{
	g_autofree gchar           *base = g_strdup_printf ("/layers[%u]", index);
	g_autoptr(LrgTerrainLayer)  layer = lrg_terrain_layer_new (NULL);
	YamlMapping                *map;
	guint                       i;

	if (!_lrg_terrain_rd_mapping (rd, node, base, &map))
		return FALSE;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", base, key);
		const gchar      *s;

		if (g_strcmp0 (key, "name") == 0 || g_strcmp0 (key, "texture") == 0 || g_strcmp0 (key, "material") == 0)
		{
			if (!_lrg_terrain_rd_string (rd, member, p, &s))
				return FALSE;
			if (key[0] == 'n')
				layer->name = g_strdup (s);
			else if (key[0] == 't')
				layer->texture = g_strdup (s);
			else
				layer->material = g_strdup (s);
		}
		else if (g_strcmp0 (key, "tiling") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.001, 100000.0, &layer->tiling))
				return FALSE;
		}
		else if (g_strcmp0 (key, "color") == 0)
		{
			gfloat c[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			guint  n, k;

			if (!_lrg_terrain_rd_numbers (rd, member, p, 3, 4, c, &n))
				return FALSE;
			for (k = 0; k < 4; k++)
				if (c[k] < 0.0f || c[k] > 1.0f)
					return _lrg_terrain_rd_fail (rd, p, "colour components must lie in 0 .. 1");
			memcpy (layer->color, c, sizeof c);
		}
		else
			_lrg_terrain_keep_extra (&layer->extra, key, member);
	}
	g_ptr_array_add (self->layers, g_steal_pointer (&layer));
	return TRUE;
}

static gboolean
read_simple_block (LrgTerrainAsset  *self,
                   TerrainReader    *rd,
                   YamlNode         *node,
                   const gchar      *name)
{
	YamlMapping *map;
	guint        i;

	if (!_lrg_terrain_rd_mapping (rd, node, name, &map))
		return FALSE;
	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar      *key = yaml_mapping_get_key (map, i);
		YamlNode         *member = yaml_mapping_get_value (map, i);
		g_autofree gchar *p = g_strdup_printf ("%s/%s", name, key);

		if (g_strcmp0 (name, "/holes") == 0)
		{
			const gchar *s;

			if (g_strcmp0 (key, "file") != 0)
			{
				_lrg_terrain_keep_extra (&self->extra_holes, key, member);
				continue;
			}
			if (!_lrg_terrain_rd_string (rd, member, p, &s))
				return FALSE;
			g_free (self->holes_file);
			self->holes_file = g_strdup (s);
			continue;
		}

		/* /lod */
		if (g_strcmp0 (key, "levels") == 0)
		{
			if (!_lrg_terrain_rd_uint (rd, member, p, 1, LRG_TERRAIN_ASSET_MAX_LOD, &self->lod_levels))
				return FALSE;
		}
		else if (g_strcmp0 (key, "distances") == 0)
		{
			gfloat d[LRG_TERRAIN_ASSET_MAX_LOD];
			guint  n, k;

			if (!_lrg_terrain_rd_numbers (rd, member, p, 0, LRG_TERRAIN_ASSET_MAX_LOD - 1, d, &n))
				return FALSE;
			for (k = 0; k < n; k++)
			{
				if (!(d[k] > 0.0f) || (k > 0 && !(d[k] > d[k - 1])))
					return _lrg_terrain_rd_fail (rd, p, "distances must be positive and increasing");
				self->lod_distances[k + 1] = d[k];
			}
		}
		else if (g_strcmp0 (key, "skirt") == 0)
		{
			if (!_lrg_terrain_rd_number (rd, member, p, 0.0, 1.0e6, &self->skirt_depth))
				return FALSE;
		}
		else
			_lrg_terrain_keep_extra (&self->extra_lod, key, member);
	}
	return TRUE;
}

/* The whole document: the grid first (everything else depends on it),
 * then every other key in order. */
static gboolean
read_document (LrgTerrainAsset *self,
               TerrainReader   *rd,
               YamlNode        *root,
               const gchar     *base_dir)
{
	YamlMapping *map;
	YamlNode    *grid, *foliage_node = NULL;
	gboolean     have_layers = FALSE;
	guint        i;

	if (root == NULL || yaml_node_get_node_type (root) != YAML_NODE_MAPPING)
		return _lrg_terrain_rd_fail (rd, "/", "a terrain must be a YAML mapping");
	map = yaml_node_get_mapping (root);
	grid = yaml_mapping_get_member (map, "grid");
	if (grid == NULL)
		return _lrg_terrain_rd_fail (rd, "/grid", "missing (not a terrain asset)");
	if (!read_grid (self, rd, grid))
		return FALSE;

	for (i = 0; i < yaml_mapping_get_size (map); i++)
	{
		const gchar *key = yaml_mapping_get_key (map, i);
		YamlNode    *member = yaml_mapping_get_value (map, i);

		if (g_strcmp0 (key, "grid") == 0)
			continue;
		if (g_strcmp0 (key, "version") == 0)
		{
			guint v;

			if (!_lrg_terrain_rd_uint (rd, member, "/version", 1, G_MAXUINT, &v))
				return FALSE;
			if (v > LRG_TERRAIN_ASSET_FORMAT_VERSION)
				return _lrg_terrain_rd_fail (rd, "/version", "version %u is newer than this build reads (%u)", v,
				                             (guint) LRG_TERRAIN_ASSET_FORMAT_VERSION);
		}
		else if (g_strcmp0 (key, "name") == 0)
		{
			const gchar *s;

			if (!_lrg_terrain_rd_string (rd, member, "/name", &s))
				return FALSE;
			lrg_terrain_asset_set_name (self, s);
		}
		else if (g_strcmp0 (key, "heights") == 0)
		{
			if (!read_heights (self, rd, member))
				return FALSE;
		}
		else if (g_strcmp0 (key, "splat") == 0)
		{
			if (!read_splat (self, rd, member))
				return FALSE;
		}
		else if (g_strcmp0 (key, "layers") == 0)
		{
			YamlSequence *seq;
			guint         j;

			if (!_lrg_terrain_rd_sequence (rd, member, "/layers", &seq))
				return FALSE;
			if (yaml_sequence_get_length (seq) < 1 || yaml_sequence_get_length (seq) > LRG_TERRAIN_ASSET_MAX_LAYERS)
				return _lrg_terrain_rd_fail (rd, "/layers", "expected 1 to %u layers",
				                             (guint) LRG_TERRAIN_ASSET_MAX_LAYERS);
			for (j = 0; j < yaml_sequence_get_length (seq); j++)
				if (!read_layer (self, rd, yaml_sequence_get_element (seq, j), j))
					return FALSE;
			have_layers = TRUE;
		}
		else if (g_strcmp0 (key, "holes") == 0)
		{
			if (!read_simple_block (self, rd, member, "/holes"))
				return FALSE;
		}
		else if (g_strcmp0 (key, "lod") == 0)
		{
			if (!read_simple_block (self, rd, member, "/lod"))
				return FALSE;
		}
		else if (g_strcmp0 (key, "bake_resolution") == 0)
		{
			if (!_lrg_terrain_rd_uint (rd, member, "/bake_resolution", 16, 2048, &self->bake_resolution))
				return FALSE;
		}
		else if (g_strcmp0 (key, "foliage") == 0)
			foliage_node = member;
		else
			_lrg_terrain_keep_extra (&self->extra, key, member);
	}
	if (!have_layers)
		g_ptr_array_add (self->layers, lrg_terrain_layer_new ("base"));

	/* sidecars (only with a directory to read them from) */
	if (base_dir != NULL)
	{
		if (self->height_file != NULL && !load_heights (self, rd, base_dir))
			return FALSE;
		if (!load_surface_sidecars (self, rd, base_dir))
			return FALSE;
	}
	if (foliage_node != NULL && !_lrg_foliage_read (self->foliage, rd, foliage_node, "/foliage", base_dir))
		return FALSE;
	return TRUE;
}

static LrgTerrainAsset *
load_text (const gchar  *data,
           gssize        length,
           const gchar  *source,
           const gchar  *base_dir,
           GError      **error)
{
	g_autoptr(LrgTerrainAsset) self = (LrgTerrainAsset *) g_object_new (LRG_TYPE_TERRAIN_ASSET, NULL);
	g_autoptr(YamlParser)      parser = yaml_parser_new ();
	g_autoptr(GError)          local = NULL;
	TerrainReader              rd;

	if (!yaml_parser_load_from_data (parser, data != NULL ? data : "", length, &local))
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_PARSE, "%s: %s", source,
		             local->message);
		return NULL;
	}
	rd.source = source;
	rd.domain = LRG_TERRAIN_ASSET_ERROR;
	rd.code = LRG_TERRAIN_ASSET_ERROR_PARSE;
	rd.error = error;
	if (!read_document (self, &rd, yaml_parser_get_root (parser), base_dir))
		return NULL;
	lrg_terrain_asset_set_base_dir (self, base_dir);
	return g_steal_pointer (&self);
}

LrgTerrainAsset *
lrg_terrain_asset_new_from_data (const gchar  *data,
                                 gssize        length,
                                 const gchar  *base_dir,
                                 GError      **error)
{
	return load_text (data, length, "<data>", base_dir, error);
}

/* The file name without directory and last extension. */
static gchar *
file_stem (const gchar *path)
{
	g_autofree gchar *base = g_path_get_basename (path);
	gchar            *dot = strrchr (base, '.');

	if (dot != NULL && dot != base)
		*dot = '\0';
	return g_strdup (base);
}

LrgTerrainAsset *
lrg_terrain_asset_new_from_file (const gchar  *path,
                                 GError      **error)
{
	g_autofree gchar  *contents = NULL;
	g_autofree gchar  *dir = NULL;
	g_autoptr(GError)  local = NULL;
	gsize              len = 0;
	LrgTerrainAsset   *self;

	g_return_val_if_fail (path != NULL, NULL);
	if (!g_file_get_contents (path, &contents, &len, &local))
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO, "%s", local->message);
		return NULL;
	}
	dir = g_path_get_dirname (path);
	self = load_text (contents, (gssize) len, path, dir, error);
	if (self != NULL)
		self->stem = file_stem (path);
	return self;
}

/* ==========================================================================
 * Saving
 * ========================================================================== */

/* Fills in sidecar names that are missing (or all of them when @renew)
 * from @stem. */
static void
ensure_names (LrgTerrainAsset *self,
              const gchar     *stem,
              gboolean         renew)
{
	guint f;

	if (renew || self->height_file == NULL)
	{
		g_free (self->height_file);
		self->height_file = g_strdup_printf ("%s.heights.%s", stem,
		                                     self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_R32 ? "r32" : "png");
	}
	for (f = 0; f < 2; f++)
		if (renew || self->splat_files[f] == NULL)
		{
			g_free (self->splat_files[f]);
			self->splat_files[f] = g_strdup_printf ("%s.splat%u.png", stem, f);
		}
	if (renew || self->ids_file == NULL)
	{
		g_free (self->ids_file);
		self->ids_file = g_strdup_printf ("%s.ids.png", stem);
	}
	if (renew || self->holes_file == NULL)
	{
		g_free (self->holes_file);
		self->holes_file = g_strdup_printf ("%s.holes.png", stem);
	}
}

/* The stem names default to: the last file's, else the name, else
 * "terrain". */
static const gchar *
default_stem (LrgTerrainAsset *self)
{
	if (self->stem != NULL)
		return self->stem;
	if (self->name != NULL && self->name[0] != '\0')
		return self->name;
	return "terrain";
}

/* Widens the PNG16 range to the data when needed. */
static void
fit_range (LrgTerrainAsset *self)
{
	gsize  n = (gsize) self->nx * self->nz, i;
	gfloat lo = self->range_min, hi = self->range_max;

	for (i = 0; i < n; i++)
	{
		lo = MIN (lo, self->heights[i]);
		hi = MAX (hi, self->heights[i]);
	}
	if (!(hi > lo))
		hi = lo + 1.0f;
	self->range_min = lo;
	self->range_max = hi;
}

static guint
n_splat_files (LrgTerrainAsset *self)
{
	return (self->layers->len + 3) / 4;
}

static gboolean
writes_splat (LrgTerrainAsset *self)
{
	return self->layers->len > 1 || self->splat_mode == LRG_TERRAIN_SPLAT_MODE_IDS;
}

/* The YAML tree.  Sidecar names must be set (ensure_names()). */
static YamlNode *
build_document (LrgTerrainAsset *self,
                const gchar     *dir,
                const gchar     *stem,
                gboolean         renew,
                GError         **error)
{
	g_autoptr(YamlNode) root = yaml_node_new_mapping (NULL);
	YamlMapping        *map = yaml_node_get_mapping (root);
	YamlNode           *node;
	YamlMapping        *m;
	gfloat              v[LRG_TERRAIN_ASSET_MAX_LOD];
	guint               i;

	_lrg_terrain_set_member (map, "version", lrg_data_yaml_int (LRG_TERRAIN_ASSET_FORMAT_VERSION));
	if (self->name != NULL)
		_lrg_terrain_set_member (map, "name", lrg_data_yaml_string (self->name));

	/* grid */
	node = yaml_node_new_mapping (NULL);
	m = yaml_node_get_mapping (node);
	_lrg_terrain_set_member (m, "resolution", lrg_data_yaml_int (self->resolution));
	_lrg_terrain_set_member (m, "chunk_size", lrg_data_yaml_float (self->chunk_size));
	v[0] = (gfloat) self->chunks_x;
	v[1] = (gfloat) self->chunks_z;
	_lrg_terrain_set_member (m, "chunks", _lrg_terrain_float_list (v, 2));
	_lrg_terrain_write_extra (m, self->extra_grid);
	_lrg_terrain_set_member (map, "grid", node);

	/* heights */
	node = yaml_node_new_mapping (NULL);
	m = yaml_node_get_mapping (node);
	_lrg_terrain_set_member (m, "file", lrg_data_yaml_string (self->height_file));
	_lrg_terrain_set_member (m, "format",
	                         lrg_data_yaml_string (_lrg_terrain_enum_nick (LRG_TYPE_TERRAIN_HEIGHT_FORMAT,
	                                                                       (gint) self->height_format)));
	if (self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_PNG16)
	{
		v[0] = self->range_min;
		v[1] = self->range_max;
		_lrg_terrain_set_member (m, "range", _lrg_terrain_float_list (v, 2));
	}
	_lrg_terrain_write_extra (m, self->extra_heights);
	_lrg_terrain_set_member (map, "heights", node);

	/* splat */
	if (writes_splat (self) || self->extra_splat != NULL)
	{
		node = yaml_node_new_mapping (NULL);
		m = yaml_node_get_mapping (node);
		_lrg_terrain_set_member (m, "mode",
		                         lrg_data_yaml_string (_lrg_terrain_enum_nick (LRG_TYPE_TERRAIN_SPLAT_MODE,
		                                                                       (gint) self->splat_mode)));
		if (self->splat_mode == LRG_TERRAIN_SPLAT_MODE_WEIGHTS && self->layers->len > 1)
		{
			YamlNode     *files = yaml_node_new_sequence (NULL);
			YamlSequence *seq = yaml_node_get_sequence (files);

			for (i = 0; i < n_splat_files (self); i++)
			{
				YamlNode *s = lrg_data_yaml_string (self->splat_files[i]);

				yaml_sequence_add_element (seq, s);
				yaml_node_unref (s);
			}
			_lrg_terrain_set_member (m, "files", files);
		}
		if (self->splat_mode == LRG_TERRAIN_SPLAT_MODE_IDS)
			_lrg_terrain_set_member (m, "ids_file", lrg_data_yaml_string (self->ids_file));
		_lrg_terrain_write_extra (m, self->extra_splat);
		_lrg_terrain_set_member (map, "splat", node);
	}

	/* layers */
	{
		YamlNode     *list = yaml_node_new_sequence (NULL);
		YamlSequence *seq = yaml_node_get_sequence (list);

		for (i = 0; i < self->layers->len; i++)
		{
			LrgTerrainLayer *layer = g_ptr_array_index (self->layers, i);
			YamlNode        *ln = yaml_node_new_mapping (NULL);
			YamlMapping     *lm = yaml_node_get_mapping (ln);

			if (layer->name != NULL)
				_lrg_terrain_set_member (lm, "name", lrg_data_yaml_string (layer->name));
			if (layer->texture != NULL)
				_lrg_terrain_set_member (lm, "texture", lrg_data_yaml_string (layer->texture));
			if (layer->material != NULL)
				_lrg_terrain_set_member (lm, "material", lrg_data_yaml_string (layer->material));
			_lrg_terrain_set_member (lm, "tiling", lrg_data_yaml_float (layer->tiling));
			_lrg_terrain_set_member (lm, "color", _lrg_terrain_float_list (layer->color, 4));
			_lrg_terrain_write_extra (lm, layer->extra);
			yaml_sequence_add_element (seq, ln);
			yaml_node_unref (ln);
		}
		_lrg_terrain_set_member (map, "layers", list);
	}

	/* holes (only when there are any) */
	if (lrg_terrain_asset_get_hole_count (self) > 0 || self->extra_holes != NULL)
	{
		node = yaml_node_new_mapping (NULL);
		m = yaml_node_get_mapping (node);
		if (lrg_terrain_asset_get_hole_count (self) > 0)
			_lrg_terrain_set_member (m, "file", lrg_data_yaml_string (self->holes_file));
		_lrg_terrain_write_extra (m, self->extra_holes);
		_lrg_terrain_set_member (map, "holes", node);
	}

	/* lod */
	node = yaml_node_new_mapping (NULL);
	m = yaml_node_get_mapping (node);
	_lrg_terrain_set_member (m, "levels", lrg_data_yaml_int (self->lod_levels));
	for (i = 1; i < self->lod_levels; i++)
		v[i - 1] = self->lod_distances[i];
	if (self->lod_levels > 1)
		_lrg_terrain_set_member (m, "distances", _lrg_terrain_float_list (v, self->lod_levels - 1));
	_lrg_terrain_set_member (m, "skirt", lrg_data_yaml_float (self->skirt_depth));
	_lrg_terrain_write_extra (m, self->extra_lod);
	_lrg_terrain_set_member (map, "lod", node);

	if (self->bake_resolution != 256)
		_lrg_terrain_set_member (map, "bake_resolution", lrg_data_yaml_int (self->bake_resolution));

	if (!_lrg_foliage_is_empty (self->foliage))
	{
		node = _lrg_foliage_write (self->foliage, dir, stem, renew, error);
		if (node == NULL)
			return NULL;
		_lrg_terrain_set_member (map, "foliage", node);
	}

	_lrg_terrain_write_extra (map, self->extra);
	return g_steal_pointer (&root);
}

gchar *
lrg_terrain_asset_to_data (LrgTerrainAsset  *self,
                           gsize            *length,
                           GError          **error)
{
	g_autoptr(YamlNode) root = NULL;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), NULL);
	ensure_names (self, default_stem (self), FALSE);
	if (self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_PNG16)
		fit_range (self);
	root = build_document (self, NULL, default_stem (self), FALSE, error);
	if (root == NULL)
		return NULL;
	return lrg_data_yaml_to_data (root, length, error);
}

/* Writes the heights sidecar. */
static gboolean
save_heights (LrgTerrainAsset  *self,
              const gchar      *dir,
              GError          **error)
{
	g_autofree gchar *file = sidecar_path (dir, self->height_file);
	gsize             n = (gsize) self->nx * self->nz, i;

	if (self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_R32)
	{
		g_autofree guint8 *buf = g_malloc (n * 4);
		g_autoptr(GError)  local = NULL;

		for (i = 0; i < n; i++)
		{
			guint32 bits;

			memcpy (&bits, &self->heights[i], 4);
			bits = GUINT32_TO_LE (bits);
			memcpy (buf + i * 4, &bits, 4);
		}
		if (!g_file_set_contents (file, (const gchar *) buf, (gssize) (n * 4), &local))
		{
			g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO, "%s", local->message);
			return FALSE;
		}
		return TRUE;
	}
	else
	{
		g_autofree guint16 *buf = g_new (guint16, n);
		gdouble             span = (gdouble) self->range_max - (gdouble) self->range_min;

		for (i = 0; i < n; i++)
		{
			gdouble q = ((gdouble) self->heights[i] - (gdouble) self->range_min) / span * 65535.0;

			buf[i] = (guint16) CLAMP (lrint (q), 0, 65535);
		}
		return _lrg_terrain_png_save (file, self->nx, self->nz, 1, 16, buf, error);
	}
}

/* Writes the splat, ids and holes sidecars the document names. */
static gboolean
save_surface (LrgTerrainAsset  *self,
              const gchar      *dir,
              GError          **error)
{
	gsize n = (gsize) self->nx * self->nz, cells = (gsize) (self->nx - 1) * (self->nz - 1), i;
	guint f, c;

	if (self->splat_mode == LRG_TERRAIN_SPLAT_MODE_WEIGHTS && self->layers->len > 1)
		for (f = 0; f < n_splat_files (self); f++)
		{
			g_autofree guint8 *buf = g_malloc (n * 4);
			g_autofree gchar  *file = sidecar_path (dir, self->splat_files[f]);

			for (i = 0; i < n; i++)
				for (c = 0; c < 4; c++)
					buf[i * 4 + c] = self->weights[i * LRG_TERRAIN_WEIGHT_STRIDE + f * 4 + c];
			if (!_lrg_terrain_png_save (file, self->nx, self->nz, 4, 8, buf, error))
				return FALSE;
		}
	if (self->splat_mode == LRG_TERRAIN_SPLAT_MODE_IDS)
	{
		g_autofree gchar *file = sidecar_path (dir, self->ids_file);

		if (!_lrg_terrain_png_save (file, self->nx - 1, self->nz - 1, 1, 8, self->ids, error))
			return FALSE;
	}
	if (lrg_terrain_asset_get_hole_count (self) > 0)
	{
		g_autofree guint8 *buf = g_malloc (cells);
		g_autofree gchar  *file = sidecar_path (dir, self->holes_file);

		for (i = 0; i < cells; i++)
			buf[i] = self->holes[i] != 0 ? 255 : 0;
		if (!_lrg_terrain_png_save (file, self->nx - 1, self->nz - 1, 1, 8, buf, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
lrg_terrain_asset_save_to_file (LrgTerrainAsset  *self,
                                const gchar      *path,
                                GError          **error)
{
	g_autofree gchar   *dir = NULL;
	g_autofree gchar   *stem = NULL;
	g_autofree gchar   *text = NULL;
	g_autoptr(YamlNode) root = NULL;
	g_autoptr(GError)   local = NULL;
	gsize               len = 0;
	gboolean            renew;

	g_return_val_if_fail (LRG_IS_TERRAIN_ASSET (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);

	/* names follow the file: a new stem renames every sidecar */
	dir = g_path_get_dirname (path);
	stem = file_stem (path);
	renew = g_strcmp0 (stem, self->stem) != 0;
	ensure_names (self, stem, renew);
	if (self->height_format == LRG_TERRAIN_HEIGHT_FORMAT_PNG16)
		fit_range (self);

	root = build_document (self, dir, stem, renew, error);
	if (root == NULL)
		return FALSE;
	text = lrg_data_yaml_to_data (root, &len, error);
	if (text == NULL)
		return FALSE;
	if (!save_heights (self, dir, error) || !save_surface (self, dir, error))
		return FALSE;
	if (!g_file_set_contents (path, text, (gssize) len, &local))
	{
		g_set_error (error, LRG_TERRAIN_ASSET_ERROR, LRG_TERRAIN_ASSET_ERROR_IO, "%s", local->message);
		return FALSE;
	}
	g_free (self->stem);
	self->stem = g_steal_pointer (&stem);
	lrg_terrain_asset_set_base_dir (self, dir);
	return TRUE;
}
