/* lrg-pack-writer.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * rres writer compatible with graylib's reader, plus VFS and export bundles.
 * Chunk layout follows deps/graylib/deps/rres/src/rres.h (RAWD + CDIR).
 */

#include "config.h"
#include "lrg-pack-writer.h"
#include "lrg-asset-meta.h"
#include "lrg-asset-pack.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <graylib.h>
#include <string.h>

#define PACK_FILE_CAP ((gsize) 64 * 1024 * 1024)
#define RRES_VERSION  ((guint16) 100)
#define CDIR_ID       ((guint32) 0xCD17D1A0u)

typedef struct
{
	guint8  id[4];
	guint16 version;
	guint16 chunk_count;
	guint32 cd_offset;
	guint32 reserved;
} RresHeader;

typedef struct
{
	guint8  type[4];
	guint32 id;
	guint8  comp;
	guint8  cipher;
	guint16 flags;
	guint32 packed_size;
	guint32 base_size;
	guint32 next_offset;
	guint32 reserved;
	guint32 crc32;
} RresChunkInfo;

G_STATIC_ASSERT (sizeof (RresHeader) == 16);
G_STATIC_ASSERT (sizeof (RresChunkInfo) == 32);

typedef struct
{
	gchar  *name;
	guint8 *data;
	gsize   size;
	gchar  *source;   /* absolute path, or NULL for add_bytes */
	gboolean cooked;
} PackItem;

struct _LrgPackWriter
{
	GObject parent_instance;

	gchar     *project;
	gchar     *module_path;
	gchar     *launcher_path;
	gchar     *app_id;
	gboolean   cook;
	GPtrArray *items;   /* PackItem *, free with item_free */
	GString   *log;
};

G_DEFINE_FINAL_TYPE (LrgPackWriter, lrg_pack_writer, G_TYPE_OBJECT)

static const gchar * const profile_ids[] = {
	"rres",
	"vfs",
	"linux-tar",
	"flatpak",
	"appimage",
	"windows-zip",
	NULL
};

/* -------------------------------------------------------------------------- */
/* Items                                                                      */
/* -------------------------------------------------------------------------- */

static void
item_free (gpointer data)
{
	PackItem *item = (PackItem *) data;

	if (item == NULL)
		return;
	g_free (item->name);
	g_free (item->data);
	g_free (item->source);
	g_free (item);
}

static PackItem *
item_new (const gchar  *name,
          const guint8 *data,
          gsize         size)
{
	PackItem *item;

	item = g_new0 (PackItem, 1);
	item->name = g_strdup (name);
	item->size = size;
	if (size > 0 && data != NULL)
	{
		item->data = g_memdup2 (data, size);
	}
	else
	{
		item->data = g_new0 (guint8, 1);
		item->size = 0;
	}
	return item;
}

static gint
item_cmp (gconstpointer a,
          gconstpointer b)
{
	const PackItem *ia = *(const PackItem * const *) a;
	const PackItem *ib = *(const PackItem * const *) b;

	return strcmp (ia->name, ib->name);
}

static PackItem *
find_item (LrgPackWriter *self,
           const gchar   *name)
{
	guint i;

	for (i = 0; i < self->items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (self->items, i);

		if (g_str_equal (item->name, name))
			return item;
	}
	return NULL;
}

static void
log_line (LrgPackWriter *self,
          const gchar   *text)
{
	g_string_append (self->log, text);
	g_string_append_c (self->log, '\n');
}

static gboolean
name_ok (const gchar  *name,
         GError      **error)
{
	if (name == NULL || name[0] == '\0' || g_path_is_absolute (name) ||
	    strstr (name, "..") != NULL || strchr (name, '\\') != NULL ||
	    strchr (name, '\n') != NULL)
	{
		g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		             "Pack path '%s' is not a relative forward-slash path",
		             (name != NULL) ? name : "(null)");
		return FALSE;
	}
	return TRUE;
}

/* -------------------------------------------------------------------------- */
/* rres                                                                       */
/* -------------------------------------------------------------------------- */

static guint32
filename_id (const gchar *name)
{
	guint32 id;

	id = grl_resource_pack_compute_crc32 ((const guint8 *) name, strlen (name));
	if (id == 0 || id == CDIR_ID)
		id = 1;
	return id;
}

static guint32
ext_word (const gchar *ext,
          guint        index)
{
	gchar   tmp[4];
	guint   i;
	const gchar *p;

	memset (tmp, 0, sizeof (tmp));
	p = ext + (index * 4);
	for (i = 0; i < 4 && p[i] != '\0'; i++)
		tmp[i] = p[i];
	return ((guint32) (guint8) tmp[0] << 24) |
	       ((guint32) (guint8) tmp[1] << 16) |
	       ((guint32) (guint8) tmp[2] << 8) |
	       (guint32) (guint8) tmp[3];
}

static gchar *
extension_of (const gchar *name)
{
	const gchar *dot;

	dot = strrchr (name, '.');
	if (dot == NULL || strchr (dot, '/') != NULL)
		return g_strdup ("");
	return g_ascii_strdown (dot, -1);
}

/*
 * RAWD payload: propCount + 4 props + bytes.
 * props[0] is the file size, props[1]/props[2] are the extension packed
 * big-endian the way rres documents (".png" == 0x2e706e67).
 */
static GByteArray *
rawd_payload (const gchar  *name,
              const guint8 *data,
              gsize         size)
{
	GByteArray *payload;
	guint32     words[5];
	g_autofree gchar *ext = NULL;

	ext = extension_of (name);
	words[0] = 4;
	words[1] = (guint32) size;
	words[2] = ext_word (ext, 0);
	words[3] = ext_word (ext, 1);
	words[4] = 0;

	payload = g_byte_array_new ();
	g_byte_array_append (payload, (const guint8 *) words, sizeof (words));
	if (size > 0)
		g_byte_array_append (payload, data, size);
	return payload;
}

static guint
name_slot (const gchar *name)
{
	guint n;

	/* NUL plus pad so the stored length is a multiple of 4. */
	n = (guint) strlen (name) + 1;
	return (n + 3u) & ~3u;
}

static GByteArray *
cdir_payload (GPtrArray *items,
              GArray    *offsets)
{
	GByteArray *payload;
	guint32     head[2];
	guint       i;

	head[0] = 1;                /* propCount */
	head[1] = items->len;       /* props[0] entry count */
	payload = g_byte_array_new ();
	g_byte_array_append (payload, (const guint8 *) head, sizeof (head));

	for (i = 0; i < items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (items, i);
		guint32   fields[4];
		guint     slot;
		guint8   *slot_bytes;

		slot = name_slot (item->name);
		fields[0] = filename_id (item->name);
		fields[1] = g_array_index (offsets, guint32, i);
		fields[2] = 0;
		fields[3] = slot;
		g_byte_array_append (payload, (const guint8 *) fields, sizeof (fields));
		slot_bytes = g_new0 (guint8, slot);
		memcpy (slot_bytes, item->name, strlen (item->name));
		g_byte_array_append (payload, slot_bytes, slot);
		g_free (slot_bytes);
	}
	return payload;
}

static void
fill_info (RresChunkInfo *info,
           const gchar   *fourcc,
           guint32        id,
           GByteArray    *payload)
{
	memset (info, 0, sizeof (*info));
	memcpy (info->type, fourcc, 4);
	info->id = id;
	info->packed_size = payload->len;
	info->base_size = payload->len;
	info->crc32 = grl_resource_pack_compute_crc32 (payload->data, payload->len);
}

static gboolean
check_ids (LrgPackWriter  *self,
           GError        **error)
{
	GHashTable *seen;
	guint       i;
	gboolean    ok;

	seen = g_hash_table_new (g_direct_hash, g_direct_equal);
	ok = TRUE;
	for (i = 0; i < self->items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (self->items, i);
		guint32   id = filename_id (item->name);

		if (g_hash_table_contains (seen, GUINT_TO_POINTER (id)))
		{
			g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
			             "Resource id collision on '%s'", item->name);
			ok = FALSE;
			break;
		}
		g_hash_table_add (seen, GUINT_TO_POINTER (id));
	}
	g_hash_table_unref (seen);
	return ok;
}

static GByteArray *
build_rres (LrgPackWriter  *self,
            GError        **error)
{
	GByteArray *out;
	GPtrArray  *payloads;
	GArray     *offsets;
	GByteArray *cdir;
	RresHeader  header;
	guint32     cursor;
	guint       i;

	if (self->items->len > 65534)
	{
		g_set_error_literal (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		                     "rres holds at most 65534 files");
		return NULL;
	}
	if (!check_ids (self, error))
		return NULL;

	g_ptr_array_sort (self->items, item_cmp);

	payloads = g_ptr_array_new_with_free_func ((GDestroyNotify) g_byte_array_unref);
	offsets = g_array_new (FALSE, FALSE, sizeof (guint32));
	cursor = sizeof (RresHeader);
	for (i = 0; i < self->items->len; i++)
	{
		PackItem   *item = (PackItem *) g_ptr_array_index (self->items, i);
		GByteArray *payload;

		g_array_append_val (offsets, cursor);
		payload = rawd_payload (item->name, item->data, item->size);
		cursor += (guint32) sizeof (RresChunkInfo) + payload->len;
		g_ptr_array_add (payloads, payload);
	}

	/* Empty packs store cdOffset 0: the reader treats 0 as "no directory",
	 * and a directory that begins at byte 16 would also be offset 0. */
	cdir = NULL;
	if (self->items->len > 0)
		cdir = cdir_payload (self->items, offsets);

	memset (&header, 0, sizeof (header));
	header.id[0] = 'r';
	header.id[1] = 'r';
	header.id[2] = 'e';
	header.id[3] = 's';
	header.version = RRES_VERSION;
	header.chunk_count = (guint16) (self->items->len + ((cdir != NULL) ? 1 : 0));
	header.cd_offset = (cdir != NULL) ? (cursor - (guint32) sizeof (RresHeader)) : 0;

	out = g_byte_array_sized_new (cursor + 64);
	g_byte_array_append (out, (const guint8 *) &header, sizeof (header));
	for (i = 0; i < payloads->len; i++)
	{
		PackItem     *item = (PackItem *) g_ptr_array_index (self->items, i);
		GByteArray   *payload = (GByteArray *) g_ptr_array_index (payloads, i);
		RresChunkInfo info;

		fill_info (&info, "RAWD", filename_id (item->name), payload);
		g_byte_array_append (out, (const guint8 *) &info, sizeof (info));
		g_byte_array_append (out, payload->data, payload->len);
	}
	if (cdir != NULL)
	{
		RresChunkInfo info;

		fill_info (&info, "CDIR", CDIR_ID, cdir);
		g_byte_array_append (out, (const guint8 *) &info, sizeof (info));
		g_byte_array_append (out, cdir->data, cdir->len);
		g_byte_array_unref (cdir);
	}
	g_ptr_array_unref (payloads);
	g_array_unref (offsets);
	return out;
}

/* -------------------------------------------------------------------------- */
/* Cooking                                                                    */
/* -------------------------------------------------------------------------- */

static gboolean
is_texture_name (const gchar *name)
{
	g_autofree gchar *lower = NULL;

	lower = g_ascii_strdown (name, -1);
	return g_str_has_suffix (lower, ".png") || g_str_has_suffix (lower, ".jpg") ||
	       g_str_has_suffix (lower, ".jpeg") || g_str_has_suffix (lower, ".bmp") ||
	       g_str_has_suffix (lower, ".tga");
}

/* RGB565 endpoints. color0 > color1 selects the opaque 4-colour mode. */
static guint16
pack565 (guint8 r,
         guint8 g,
         guint8 b)
{
	return (guint16) (((guint16) (r >> 3) << 11) | ((guint16) (g >> 2) << 5) | (guint16) (b >> 3));
}

static void
unpack565 (guint16 c,
           gint   *r,
           gint   *g,
           gint   *b)
{
	*r = (gint) (((c >> 11) & 31) * 255 / 31);
	*g = (gint) (((c >> 5) & 63) * 255 / 63);
	*b = (gint) ((c & 31) * 255 / 31);
}

static void
sample_rgba (const guint8 *px,
             gint          w,
             gint          h,
             gint          x,
             gint          y,
             guint8       *rgba)
{
	const guint8 *p;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= w)
		x = w - 1;
	if (y >= h)
		y = h - 1;
	p = px + ((gsize) y * (gsize) w + (gsize) x) * 4;
	rgba[0] = p[0];
	rgba[1] = p[1];
	rgba[2] = p[2];
	rgba[3] = p[3];
}

/*
 * One 4x4 DXT1 block. Endpoints are the darkest and lightest texels.
 * Indices are two bits per texel, row-major, least-significant bits first.
 */
static void
encode_dxt1_block (const guint8 *px,
                   gint          w,
                   gint          h,
                   gint          ox,
                   gint          oy,
                   guint8       *dst)
{
	guint8  texels[16][4];
	gint    min_l;
	gint    max_l;
	gint    min_i;
	gint    max_i;
	gint    ty;
	gint    tx;
	gint    i;
	guint16 c0;
	guint16 c1;
	gint    e0[3];
	gint    e1[3];
	gint    mid0[3];
	gint    mid1[3];
	guint32 bits;

	min_l = G_MAXINT;
	max_l = -1;
	min_i = 0;
	max_i = 0;
	i = 0;
	for (ty = 0; ty < 4; ty++)
	{
		for (tx = 0; tx < 4; tx++)
		{
			gint lum;

			sample_rgba (px, w, h, ox + tx, oy + ty, texels[i]);
			lum = (gint) texels[i][0] * 3 + (gint) texels[i][1] * 6 + (gint) texels[i][2];
			if (lum < min_l)
			{
				min_l = lum;
				min_i = i;
			}
			if (lum > max_l)
			{
				max_l = lum;
				max_i = i;
			}
			i++;
		}
	}

	c0 = pack565 (texels[max_i][0], texels[max_i][1], texels[max_i][2]);
	c1 = pack565 (texels[min_i][0], texels[min_i][1], texels[min_i][2]);
	if (c0 == c1)
	{
		if (c0 == 0)
			c0 = 1;
		else
			c1 = (guint16) (c0 - 1);
	}
	else if (c0 < c1)
	{
		guint16 swap = c0;

		c0 = c1;
		c1 = swap;
	}
	unpack565 (c0, &e0[0], &e0[1], &e0[2]);
	unpack565 (c1, &e1[0], &e1[1], &e1[2]);
	for (i = 0; i < 3; i++)
	{
		mid0[i] = (2 * e0[i] + e1[i]) / 3;
		mid1[i] = (e0[i] + 2 * e1[i]) / 3;
	}

	bits = 0;
	for (i = 0; i < 16; i++)
	{
		gint dist[4];
		gint best;
		gint bi;
		gint ch;

		dist[0] = 0;
		dist[1] = 0;
		dist[2] = 0;
		dist[3] = 0;
		for (ch = 0; ch < 3; ch++)
		{
			gint d0 = (gint) texels[i][ch] - e0[ch];
			gint d1 = (gint) texels[i][ch] - e1[ch];
			gint d2 = (gint) texels[i][ch] - mid0[ch];
			gint d3 = (gint) texels[i][ch] - mid1[ch];

			dist[0] += d0 * d0;
			dist[1] += d1 * d1;
			dist[2] += d2 * d2;
			dist[3] += d3 * d3;
		}
		best = dist[0];
		bi = 0;
		if (dist[1] < best)
		{
			best = dist[1];
			bi = 1;
		}
		if (dist[2] < best)
		{
			best = dist[2];
			bi = 2;
		}
		if (dist[3] < best)
			bi = 3;
		bits |= ((guint32) bi) << (i * 2);
	}

	dst[0] = (guint8) (c0 & 0xff);
	dst[1] = (guint8) ((c0 >> 8) & 0xff);
	dst[2] = (guint8) (c1 & 0xff);
	dst[3] = (guint8) ((c1 >> 8) & 0xff);
	dst[4] = (guint8) (bits & 0xff);
	dst[5] = (guint8) ((bits >> 8) & 0xff);
	dst[6] = (guint8) ((bits >> 16) & 0xff);
	dst[7] = (guint8) ((bits >> 24) & 0xff);
}

/* "DXT1" + uint32le width + uint32le height + 8-byte 4x4 blocks. */
static guint8 *
encode_dxt1 (const guint8 *px,
             gint          w,
             gint          h,
             gsize        *out_size)
{
	gint    bw;
	gint    bh;
	gint    by;
	gint    bx;
	gsize   n;
	guint8 *out;
	guint8 *dst;

	if (px == NULL || w < 1 || h < 1)
		return NULL;
	bw = (w + 3) / 4;
	bh = (h + 3) / 4;
	n = 12 + (gsize) bw * (gsize) bh * 8;
	out = g_malloc (n);
	out[0] = 'D';
	out[1] = 'X';
	out[2] = 'T';
	out[3] = '1';
	out[4] = (guint8) (w & 0xff);
	out[5] = (guint8) ((w >> 8) & 0xff);
	out[6] = (guint8) ((w >> 16) & 0xff);
	out[7] = (guint8) ((w >> 24) & 0xff);
	out[8] = (guint8) (h & 0xff);
	out[9] = (guint8) ((h >> 8) & 0xff);
	out[10] = (guint8) ((h >> 16) & 0xff);
	out[11] = (guint8) ((h >> 24) & 0xff);
	dst = out + 12;
	for (by = 0; by < bh; by++)
	{
		for (bx = 0; bx < bw; bx++)
		{
			encode_dxt1_block (px, w, h, bx * 4, by * 4, dst);
			dst += 8;
		}
	}
	*out_size = n;
	return out;
}

static void
replace_item_bytes (PackItem *item,
                    guint8   *encoded,
                    gsize     encoded_size)
{
	g_free (item->data);
	item->data = encoded;
	item->size = encoded_size;
	item->cooked = TRUE;
}

static void
cook_item (LrgPackWriter *self,
           PackItem      *item)
{
	g_autofree gchar        *meta_path = NULL;
	g_autofree gchar        *compression = NULL;
	g_autoptr(LrgAssetMeta)  meta = NULL;
	g_autoptr(GError)        error = NULL;
	GrlImage                *image;
	GrlImage                *scaled;
	guint8                  *encoded;
	guint8                  *pixels;
	gsize                    encoded_size;
	guint                    max_size;
	gint                     w;
	gint                     h;
	gint                     edge;
	gint                     nw;
	gint                     nh;
	gboolean                 want_dxt;
	g_autofree gchar        *ext = NULL;

	if (!self->cook || item->source == NULL || !is_texture_name (item->name))
		return;

	meta_path = lrg_asset_meta_path_for_asset (item->source);
	if (!g_file_test (meta_path, G_FILE_TEST_IS_REGULAR))
		return;
	meta = lrg_asset_meta_new_from_file (meta_path, &error);
	if (meta == NULL)
	{
		log_line (self, error->message);
		return;
	}
	max_size = lrg_asset_meta_get_texture_max_size (meta);
	compression = lrg_asset_meta_get_setting_string (meta, "compression");
	want_dxt = compression != NULL && g_ascii_strcasecmp (compression, "dxt1") == 0;
	if (max_size == 0 && !want_dxt)
		return;

	image = grl_image_new_from_file (item->source);
	if (image == NULL)
	{
		log_line (self, "cook: image decode failed, packing original bytes");
		return;
	}
	w = grl_image_get_width (image);
	h = grl_image_get_height (image);
	if (w <= 0 || h <= 0)
	{
		g_object_unref (image);
		return;
	}
	edge = (w > h) ? w : h;
	scaled = NULL;
	if (max_size > 0 && edge > (gint) max_size)
	{
		nw = (gint) ((gdouble) w * (gdouble) max_size / (gdouble) edge);
		nh = (gint) ((gdouble) h * (gdouble) max_size / (gdouble) edge);
		if (nw < 1)
			nw = 1;
		if (nh < 1)
			nh = 1;
		scaled = grl_image_resized (image, nw, nh);
		g_object_unref (image);
		image = NULL;
		if (scaled == NULL)
		{
			log_line (self, "cook: resize failed, packing original bytes");
			return;
		}
		w = nw;
		h = nh;
		log_line (self, "cook: resized texture to max-size");
	}

	if (want_dxt)
	{
		pixels = grl_image_get_pixels ((scaled != NULL) ? scaled : image, NULL);
		encoded = encode_dxt1 (pixels, w, h, &encoded_size);
		if (scaled != NULL)
			g_object_unref (scaled);
		else
			g_object_unref (image);
		if (encoded == NULL)
		{
			log_line (self, "cook: dxt1 encode failed, packing original bytes");
			return;
		}
		replace_item_bytes (item, encoded, encoded_size);
		log_line (self, "cook: compressed texture dxt1");
		return;
	}

	if (scaled == NULL)
	{
		g_object_unref (image);
		return;
	}
	ext = extension_of (item->name);
	encoded = grl_image_export_to_memory (scaled, ext, &encoded_size);
	g_object_unref (scaled);
	if (encoded == NULL || encoded_size == 0)
	{
		g_free (encoded);
		log_line (self, "cook: re-encode failed, packing original bytes");
		return;
	}
	replace_item_bytes (item, encoded, encoded_size);
}

static void
cook_all (LrgPackWriter *self)
{
	guint i;

	for (i = 0; i < self->items->len; i++)
		cook_item (self, (PackItem *) g_ptr_array_index (self->items, i));
}

/* -------------------------------------------------------------------------- */
/* Archives                                                                   */
/* -------------------------------------------------------------------------- */

static void
put_u16 (GByteArray *b,
         guint16     v)
{
	guint8 p[2];

	p[0] = (guint8) (v & 0xff);
	p[1] = (guint8) ((v >> 8) & 0xff);
	g_byte_array_append (b, p, 2);
}

static void
put_u32 (GByteArray *b,
         guint32     v)
{
	guint8 p[4];

	p[0] = (guint8) (v & 0xff);
	p[1] = (guint8) ((v >> 8) & 0xff);
	p[2] = (guint8) ((v >> 16) & 0xff);
	p[3] = (guint8) ((v >> 24) & 0xff);
	g_byte_array_append (b, p, 4);
}

typedef struct
{
	gchar  *name;
	guint8 *data;
	gsize   size;
} ArchFile;

static void
arch_free (gpointer data)
{
	ArchFile *file = (ArchFile *) data;

	g_free (file->name);
	g_free (file->data);
	g_free (file);
}

static void
arch_add (GPtrArray    *files,
          const gchar  *name,
          const guint8 *data,
          gsize         size)
{
	ArchFile *file;

	file = g_new0 (ArchFile, 1);
	file->name = g_strdup (name);
	file->size = size;
	file->data = (size > 0) ? g_memdup2 (data, size) : g_new0 (guint8, 1);
	g_ptr_array_add (files, file);
}

static void
arch_add_text (GPtrArray   *files,
               const gchar *name,
               const gchar *text)
{
	arch_add (files, name, (const guint8 *) text, strlen (text));
}

static gboolean
gzip_bytes (const guint8  *data,
            gsize          len,
            GByteArray   **out,
            GError       **error)
{
	g_autoptr(GZlibCompressor) comp = NULL;
	g_autoptr(GOutputStream) raw = NULL;
	g_autoptr(GOutputStream) conv = NULL;
	GBytes *bytes;
	gsize written;

	comp = g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1);
	raw = g_memory_output_stream_new_resizable ();
	conv = g_converter_output_stream_new (raw, G_CONVERTER (comp));
	if (!g_output_stream_write_all (conv, data, len, &written, NULL, error))
		return FALSE;
	if (!g_output_stream_close (conv, NULL, error))
		return FALSE;
	bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (raw));
	*out = g_byte_array_new ();
	g_byte_array_append (*out, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
	g_bytes_unref (bytes);
	return TRUE;
}

static void
tar_octal (gchar  *field,
           gsize   width,
           guint64 value)
{
	gchar buf[32];

	g_snprintf (buf, sizeof (buf), "%0*llo", (gint) (width - 1), (unsigned long long) value);
	memcpy (field, buf, width);
}

static GByteArray *
build_tar (GPtrArray *files)
{
	GByteArray *tar;
	guint       i;

	tar = g_byte_array_new ();
	for (i = 0; i < files->len; i++)
	{
		ArchFile *file = (ArchFile *) g_ptr_array_index (files, i);
		gchar     header[512];
		guint     sum;
		guint     b;
		gsize     pad;

		memset (header, 0, sizeof (header));
		g_strlcpy (header + 0, file->name, 100);
		tar_octal (header + 100, 8, 0644);
		tar_octal (header + 108, 8, 0);
		tar_octal (header + 116, 8, 0);
		tar_octal (header + 124, 12, file->size);
		tar_octal (header + 136, 12, 0);
		memset (header + 148, ' ', 8);
		header[156] = '0';
		memcpy (header + 257, "ustar", 5);
		memcpy (header + 263, "00", 2);
		sum = 0;
		for (b = 0; b < 512; b++)
			sum += (guint8) header[b];
		g_snprintf (header + 148, 8, "%06o", sum);
		header[154] = '\0';
		header[155] = ' ';
		g_byte_array_append (tar, (const guint8 *) header, 512);
		if (file->size > 0)
			g_byte_array_append (tar, file->data, file->size);
		pad = (512 - (file->size % 512)) % 512;
		if (pad > 0)
		{
			guint8 *zeros = g_new0 (guint8, pad);

			g_byte_array_append (tar, zeros, pad);
			g_free (zeros);
		}
	}
	{
		guint8 zeros[1024];

		memset (zeros, 0, sizeof (zeros));
		g_byte_array_append (tar, zeros, sizeof (zeros));
	}
	return tar;
}

static GByteArray *
build_zip (GPtrArray *files)
{
	GByteArray *zip;
	GArray     *local_off;
	guint       i;
	guint32     cd_off;
	guint32     cd_start;

	zip = g_byte_array_new ();
	local_off = g_array_new (FALSE, FALSE, sizeof (guint32));
	for (i = 0; i < files->len; i++)
	{
		ArchFile *file = (ArchFile *) g_ptr_array_index (files, i);
		guint32   crc;
		guint32   off;
		guint16   namelen;

		off = zip->len;
		g_array_append_val (local_off, off);
		crc = grl_resource_pack_compute_crc32 (file->data, file->size);
		namelen = (guint16) strlen (file->name);
		put_u32 (zip, 0x04034b50u);
		put_u16 (zip, 20);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u32 (zip, crc);
		put_u32 (zip, (guint32) file->size);
		put_u32 (zip, (guint32) file->size);
		put_u16 (zip, namelen);
		put_u16 (zip, 0);
		g_byte_array_append (zip, (const guint8 *) file->name, namelen);
		if (file->size > 0)
			g_byte_array_append (zip, file->data, file->size);
	}
	cd_start = zip->len;
	for (i = 0; i < files->len; i++)
	{
		ArchFile *file = (ArchFile *) g_ptr_array_index (files, i);
		guint32   crc;
		guint16   namelen;

		crc = grl_resource_pack_compute_crc32 (file->data, file->size);
		namelen = (guint16) strlen (file->name);
		put_u32 (zip, 0x02014b50u);
		put_u16 (zip, 20);
		put_u16 (zip, 20);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u32 (zip, crc);
		put_u32 (zip, (guint32) file->size);
		put_u32 (zip, (guint32) file->size);
		put_u16 (zip, namelen);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u16 (zip, 0);
		put_u32 (zip, 0);
		put_u32 (zip, g_array_index (local_off, guint32, i));
		g_byte_array_append (zip, (const guint8 *) file->name, namelen);
	}
	cd_off = zip->len - cd_start;
	put_u32 (zip, 0x06054b50u);
	put_u16 (zip, 0);
	put_u16 (zip, 0);
	put_u16 (zip, (guint16) files->len);
	put_u16 (zip, (guint16) files->len);
	put_u32 (zip, cd_off);
	put_u32 (zip, cd_start);
	put_u16 (zip, 0);
	g_array_unref (local_off);
	return zip;
}

static gchar *
yaml_escape (const gchar *text)
{
	GString *s;
	const gchar *p;

	s = g_string_new ("");
	for (p = (text != NULL) ? text : ""; *p != '\0'; p++)
	{
		if (*p == '\\' || *p == '"')
			g_string_append_c (s, '\\');
		g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

static gchar *
manifest_text (LrgPackWriter *self)
{
	GString *s;
	guint    i;

	s = g_string_new ("files:\n");
	for (i = 0; i < self->items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (self->items, i);
		g_autofree gchar *esc = NULL;
		g_autofree gchar *sum = NULL;

		esc = yaml_escape (item->name);
		sum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, item->data, item->size);
		g_string_append_printf (s,
		                        "  - path: \"%s\"\n    sha256: \"%s\"\n    size: %" G_GSIZE_FORMAT "\n    cooked: %s\n",
		                        esc, sum, item->size, item->cooked ? "true" : "false");
	}
	return g_string_free (s, FALSE);
}

static gboolean
read_path (const gchar  *path,
           guint8      **data,
           gsize        *size,
           GError      **error)
{
	if (!g_file_get_contents (path, (gchar **) data, size, error))
		return FALSE;
	return TRUE;
}

static gchar *
run_script (LrgPackWriter *self)
{
	if (self->launcher_path != NULL)
		return g_strdup ("#!/bin/sh\ncd \"$(dirname \"$0\")\" || exit 1\nexec ./lrgldr ${MODULE:-./game.so} -- --pack ./pack.rres \"$@\"\n");
	return g_strdup ("#!/bin/sh\ncd \"$(dirname \"$0\")\" || exit 1\nexec lrgldr ${MODULE:-./game.so} -- --pack ./pack.rres \"$@\"\n");
}

static gboolean
fill_bundle (LrgPackWriter  *self,
             GPtrArray      *files,
             GError        **error)
{
	g_autoptr(GByteArray) rres = NULL;
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *script = NULL;
	g_autofree guint8 *extra = NULL;
	gsize extra_size;

	rres = build_rres (self, error);
	if (rres == NULL)
		return FALSE;
	manifest = manifest_text (self);
	script = run_script (self);
	arch_add (files, "pack.rres", rres->data, rres->len);
	arch_add_text (files, "manifest.yaml", manifest);
	arch_add_text (files, "run.sh", script);
	arch_add_text (files, "run.bat", "lrgldr.exe %MODULE% -- --pack pack.rres\r\n");
	if (self->launcher_path != NULL &&
	    read_path (self->launcher_path, &extra, &extra_size, error))
	{
		arch_add (files, "lrgldr", extra, extra_size);
		g_clear_pointer (&extra, g_free);
	}
	else if (self->launcher_path != NULL)
		return FALSE;
	if (self->module_path != NULL &&
	    read_path (self->module_path, &extra, &extra_size, error))
	{
		arch_add (files, "game.so", extra, extra_size);
		g_free (extra);
	}
	else if (self->module_path != NULL)
		return FALSE;
	return TRUE;
}

static gboolean
write_bytes (const gchar  *path,
             const guint8 *data,
             gsize         size,
             GError      **error)
{
	g_autofree gchar *parent = NULL;

	parent = g_path_get_dirname (path);
	if (g_mkdir_with_parents (parent, 0755) != 0)
	{
		g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		             "Could not create '%s'", parent);
		return FALSE;
	}
	return g_file_set_contents (path, (const gchar *) data, size, error);
}

static gchar *
flatpak_manifest (LrgPackWriter *self)
{
	const gchar *id;

	id = (self->app_id != NULL) ? self->app_id : "org.libregnum.Game";
	return g_strdup_printf (
		"app-id: %s\n"
		"runtime: org.freedesktop.Platform\n"
		"runtime-version: '24.08'\n"
		"sdk: org.freedesktop.Sdk\n"
		"command: lrgldr\n"
		"modules:\n"
		"  - name: game\n"
		"    buildsystem: simple\n"
		"    build-commands:\n"
		"      - install -Dm644 pack.rres /app/share/game/pack.rres\n"
		"      - install -Dm755 run.sh /app/bin/run-game\n"
		"    sources:\n"
		"      - type: file\n"
		"        path: pack.rres\n"
		"      - type: file\n"
		"        path: run.sh\n",
		id);
}

/* -------------------------------------------------------------------------- */
/* Scan                                                                       */
/* -------------------------------------------------------------------------- */

static gboolean
skip_dir_name (const gchar *name)
{
	return g_str_equal (name, ".git") || g_str_equal (name, ".hg") ||
	       g_str_equal (name, ".svn") || g_str_equal (name, "build");
}

static gboolean
scan_dir (LrgPackWriter  *self,
          const gchar    *dir,
          const gchar    *rel,
          GError        **error)
{
	GDir  *handle;
	const gchar *name;

	handle = g_dir_open (dir, 0, error);
	if (handle == NULL)
		return FALSE;
	while ((name = g_dir_read_name (handle)) != NULL)
	{
		g_autofree gchar *child = NULL;
		g_autofree gchar *child_rel = NULL;

		if (skip_dir_name (name))
			continue;
		child = g_build_filename (dir, name, NULL);
		child_rel = (rel[0] == '\0') ? g_strdup (name) : g_build_filename (rel, name, NULL);
		g_strdelimit (child_rel, "\\", '/');
		if (g_file_test (child, G_FILE_TEST_IS_SYMLINK))
		{
			log_line (self, "scan: skip symlink");
			continue;
		}
		if (g_file_test (child, G_FILE_TEST_IS_DIR))
		{
			if (!scan_dir (self, child, child_rel, error))
			{
				g_dir_close (handle);
				return FALSE;
			}
			continue;
		}
		if (!g_file_test (child, G_FILE_TEST_IS_REGULAR))
			continue;
		{
			g_autofree guint8 *bytes = NULL;
			gsize size = 0;
			PackItem *item;

			if (!g_file_get_contents (child, (gchar **) &bytes, &size, error))
			{
				g_dir_close (handle);
				return FALSE;
			}
			if (size > PACK_FILE_CAP)
			{
				log_line (self, "scan: skip file over 64 MiB");
				continue;
			}
			item = item_new (child_rel, bytes, size);
			item->source = g_strdup (child);
			g_ptr_array_add (self->items, item);
		}
	}
	g_dir_close (handle);
	return TRUE;
}

/* -------------------------------------------------------------------------- */
/* Type                                                                       */
/* -------------------------------------------------------------------------- */

static void
lrg_pack_writer_dispose (GObject *object)
{
	LrgPackWriter *self = LRG_PACK_WRITER (object);

	g_clear_pointer (&self->project, g_free);
	g_clear_pointer (&self->module_path, g_free);
	g_clear_pointer (&self->launcher_path, g_free);
	g_clear_pointer (&self->app_id, g_free);
	g_clear_pointer (&self->items, g_ptr_array_unref);
	if (self->log != NULL)
	{
		g_string_free (self->log, TRUE);
		self->log = NULL;
	}
	G_OBJECT_CLASS (lrg_pack_writer_parent_class)->dispose (object);
}

static void
lrg_pack_writer_class_init (LrgPackWriterClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = lrg_pack_writer_dispose;
}

static void
lrg_pack_writer_init (LrgPackWriter *self)
{
	self->cook = TRUE;
	self->items = g_ptr_array_new_with_free_func (item_free);
	self->log = g_string_new ("");
	self->app_id = g_strdup ("org.libregnum.Game");
}

GQuark
lrg_pack_writer_error_quark (void)
{
	return g_quark_from_static_string ("lrg-pack-writer-error");
}

LrgPackWriter *
lrg_pack_writer_new (void)
{
	return g_object_new (LRG_TYPE_PACK_WRITER, NULL);
}

void
lrg_pack_writer_set_project (LrgPackWriter *self,
                             const gchar   *root)
{
	g_return_if_fail (LRG_IS_PACK_WRITER (self));

	g_free (self->project);
	self->project = g_strdup (root);
}

const gchar *
lrg_pack_writer_get_project (LrgPackWriter *self)
{
	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), NULL);
	return self->project;
}

void
lrg_pack_writer_set_module (LrgPackWriter *self,
                            const gchar   *path)
{
	g_return_if_fail (LRG_IS_PACK_WRITER (self));
	g_free (self->module_path);
	self->module_path = g_strdup (path);
}

void
lrg_pack_writer_set_launcher (LrgPackWriter *self,
                              const gchar   *path)
{
	g_return_if_fail (LRG_IS_PACK_WRITER (self));
	g_free (self->launcher_path);
	self->launcher_path = g_strdup (path);
}

void
lrg_pack_writer_set_app_id (LrgPackWriter *self,
                            const gchar   *app_id)
{
	g_return_if_fail (LRG_IS_PACK_WRITER (self));
	g_free (self->app_id);
	self->app_id = g_strdup ((app_id != NULL) ? app_id : "org.libregnum.Game");
}

void
lrg_pack_writer_set_cook (LrgPackWriter *self,
                          gboolean       cook)
{
	g_return_if_fail (LRG_IS_PACK_WRITER (self));
	self->cook = cook;
}

gboolean
lrg_pack_writer_add_bytes (LrgPackWriter  *self,
                           const gchar    *name,
                           const guint8   *data,
                           gsize           size,
                           GError        **error)
{
	PackItem *existing;

	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), FALSE);
	if (!name_ok (name, error))
		return FALSE;
	if (size > 0 && data == NULL)
	{
		g_set_error_literal (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		                     "add_bytes got a size with no buffer");
		return FALSE;
	}
	existing = find_item (self, name);
	if (existing != NULL)
		g_ptr_array_remove (self->items, existing);
	g_ptr_array_add (self->items, item_new (name, data, size));
	return TRUE;
}

gboolean
lrg_pack_writer_scan (LrgPackWriter  *self,
                      GError        **error)
{
	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), FALSE);
	if (self->project == NULL)
	{
		g_set_error_literal (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		                     "No project root set");
		return FALSE;
	}
	g_ptr_array_set_size (self->items, 0);
	return scan_dir (self, self->project, "", error);
}

guint
lrg_pack_writer_get_count (LrgPackWriter *self)
{
	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), 0);
	return self->items->len;
}

GStrv
lrg_pack_writer_get_files (LrgPackWriter *self)
{
	GStrv  out;
	guint  i;

	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), NULL);
	g_ptr_array_sort (self->items, item_cmp);
	out = g_new0 (gchar *, self->items->len + 1);
	for (i = 0; i < self->items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (self->items, i);

		out[i] = g_strdup (item->name);
	}
	return out;
}

const gchar *
lrg_pack_writer_get_log (LrgPackWriter *self)
{
	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), "");
	return self->log->str;
}

gboolean
lrg_pack_writer_write_rres (LrgPackWriter  *self,
                            const gchar    *path,
                            GError        **error)
{
	g_autoptr(GByteArray) rres = NULL;

	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	cook_all (self);
	rres = build_rres (self, error);
	if (rres == NULL)
		return FALSE;
	return write_bytes (path, rres->data, rres->len, error);
}

gboolean
lrg_pack_writer_write_vfs (LrgPackWriter  *self,
                           const gchar    *dir,
                           GError        **error)
{
	g_autofree gchar *manifest = NULL;
	g_autofree gchar *index_path = NULL;
	g_autofree gchar *objects = NULL;
	guint i;

	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), FALSE);
	g_return_val_if_fail (dir != NULL, FALSE);
	cook_all (self);
	if (g_mkdir_with_parents (dir, 0755) != 0)
	{
		g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		             "Could not create '%s'", dir);
		return FALSE;
	}
	objects = g_build_filename (dir, "objects", NULL);
	if (g_mkdir_with_parents (objects, 0755) != 0)
	{
		g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
		             "Could not create '%s'", objects);
		return FALSE;
	}
	for (i = 0; i < self->items->len; i++)
	{
		PackItem *item = (PackItem *) g_ptr_array_index (self->items, i);
		g_autofree gchar *sum = NULL;
		g_autofree gchar *obj = NULL;

		sum = g_compute_checksum_for_data (G_CHECKSUM_SHA256, item->data, item->size);
		obj = g_build_filename (objects, sum, NULL);
		if (!g_file_test (obj, G_FILE_TEST_IS_REGULAR))
		{
			if (!g_file_set_contents (obj, (const gchar *) item->data, item->size, error))
				return FALSE;
		}
	}
	manifest = manifest_text (self);
	index_path = g_build_filename (dir, "index.yaml", NULL);
	return g_file_set_contents (index_path, manifest, -1, error);
}

const gchar * const *
lrg_pack_writer_profile_ids (void)
{
	return profile_ids;
}

static gboolean
profile_known (const gchar *profile)
{
	guint i;

	for (i = 0; profile_ids[i] != NULL; i++)
		if (g_str_equal (profile_ids[i], profile))
			return TRUE;
	return FALSE;
}

gboolean
lrg_pack_writer_export (LrgPackWriter  *self,
                        const gchar    *profile,
                        const gchar    *dest,
                        GError        **error)
{
	g_return_val_if_fail (LRG_IS_PACK_WRITER (self), FALSE);
	g_return_val_if_fail (dest != NULL, FALSE);

	if (profile == NULL || !profile_known (profile))
	{
		g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_UNSUPPORTED,
		             "Unknown export profile '%s'", (profile != NULL) ? profile : "(null)");
		return FALSE;
	}
	if (g_str_equal (profile, "rres"))
		return lrg_pack_writer_write_rres (self, dest, error);
	if (g_str_equal (profile, "vfs"))
		return lrg_pack_writer_write_vfs (self, dest, error);

	cook_all (self);
	if (g_str_equal (profile, "linux-tar") || g_str_equal (profile, "windows-zip"))
	{
		g_autoptr(GPtrArray) files = NULL;
		g_autoptr(GByteArray) archive = NULL;
		g_autoptr(GByteArray) gz = NULL;

		files = g_ptr_array_new_with_free_func (arch_free);
		if (!fill_bundle (self, files, error))
			return FALSE;
		if (g_str_equal (profile, "windows-zip"))
		{
			archive = build_zip (files);
			return write_bytes (dest, archive->data, archive->len, error);
		}
		archive = build_tar (files);
		if (!gzip_bytes (archive->data, archive->len, &gz, error))
			return FALSE;
		return write_bytes (dest, gz->data, gz->len, error);
	}
	if (g_str_equal (profile, "flatpak") || g_str_equal (profile, "appimage"))
	{
		g_autoptr(GByteArray) rres = NULL;
		g_autofree gchar *script = NULL;
		g_autofree gchar *rres_path = NULL;
		g_autofree gchar *script_path = NULL;
		g_autofree gchar *man_path = NULL;
		g_autofree gchar *fp = NULL;

		if (g_mkdir_with_parents (dest, 0755) != 0)
		{
			g_set_error (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_FAILED,
			             "Could not create '%s'", dest);
			return FALSE;
		}
		rres = build_rres (self, error);
		if (rres == NULL)
			return FALSE;
		script = run_script (self);
		rres_path = g_build_filename (dest, "pack.rres", NULL);
		script_path = g_build_filename (dest, g_str_equal (profile, "appimage") ? "AppRun" : "run.sh", NULL);
		if (!g_file_set_contents (rres_path, (const gchar *) rres->data, rres->len, error))
			return FALSE;
		if (!g_file_set_contents (script_path, script, -1, error))
			return FALSE;
		if (g_chmod (script_path, 0755) != 0)
			log_line (self, "export: could not mark the launcher script executable");
		if (g_str_equal (profile, "flatpak"))
		{
			fp = flatpak_manifest (self);
			man_path = g_build_filename (dest, "manifest.yaml", NULL);
			if (!g_file_set_contents (man_path, fp, -1, error))
				return FALSE;
			log_line (self, "export: wrote a Flatpak manifest; flatpak-builder was not run");
		}
		else
		{
			g_autofree gchar *desktop = NULL;
			g_autofree gchar *desktop_path = NULL;
			gchar *tool;

			desktop = g_strdup_printf (
				"[Desktop Entry]\nType=Application\nName=Game\nExec=AppRun\nIcon=game\nCategories=Game;\n");
			desktop_path = g_build_filename (dest, "game.desktop", NULL);
			if (!g_file_set_contents (desktop_path, desktop, -1, error))
				return FALSE;
			tool = g_find_program_in_path ("appimagetool");
			if (tool == NULL)
				log_line (self, "export: appimagetool is not on PATH; left an AppDir");
			else
			{
				g_autofree gchar *image = NULL;
				g_autoptr(GError) spawn_error = NULL;
				gchar *argv[4];
				gint status = 0;

				image = g_strconcat (dest, ".AppImage", NULL);
				argv[0] = (gchar *) tool;
				argv[1] = (gchar *) dest;
				argv[2] = image;
				argv[3] = NULL;
				if (!g_spawn_sync (NULL, argv, NULL, G_SPAWN_DEFAULT, NULL, NULL,
				                   NULL, NULL, &status, &spawn_error) ||
				    status != 0)
					log_line (self, "export: appimagetool failed; left an AppDir");
				else
					log_line (self, "export: wrote an AppImage next to the AppDir");
				g_free (tool);
			}
		}
		return TRUE;
	}
	g_set_error_literal (error, LRG_PACK_WRITER_ERROR, LRG_PACK_WRITER_ERROR_UNSUPPORTED,
	                     "Unhandled profile");
	return FALSE;
}
