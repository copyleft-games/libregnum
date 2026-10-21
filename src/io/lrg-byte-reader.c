/* lrg-byte-reader.c - Bounds-checked reading of binary data
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Every read goes through need(), which checks the remaining length
 * before any byte is touched, so no read can pass the end and a failed
 * read leaves the cursor where it was.
 */

#include "config.h"
#include "lrg-byte-reader.h"

#include <string.h>

struct _LrgByteReader
{
    gatomicrefcount  ref_count;
    GBytes          *bytes;
    const guint8    *data;
    gsize            size;
    gsize            pos;
    LrgByteOrder     order;
};

G_DEFINE_QUARK (lrg-byte-error-quark, lrg_byte_error)

G_DEFINE_ENUM_TYPE (LrgByteOrder, lrg_byte_order,
    G_DEFINE_ENUM_VALUE (LRG_BYTE_ORDER_LITTLE_ENDIAN, "little-endian"),
    G_DEFINE_ENUM_VALUE (LRG_BYTE_ORDER_BIG_ENDIAN, "big-endian"))

G_DEFINE_BOXED_TYPE (LrgByteReader, lrg_byte_reader, lrg_byte_reader_ref, lrg_byte_reader_unref)

LrgByteReader *
lrg_byte_reader_new (GBytes       *bytes,
                     LrgByteOrder  order)
{
    LrgByteReader *self;

    g_return_val_if_fail (bytes != NULL, NULL);

    self = g_new0 (LrgByteReader, 1);
    g_atomic_ref_count_init (&self->ref_count);
    self->bytes = g_bytes_ref (bytes);
    self->data = g_bytes_get_data (bytes, &self->size);
    self->pos = 0;
    self->order = order;
    return self;
}

LrgByteReader *
lrg_byte_reader_new_from_data (const guint8 *data,
                               gsize         length,
                               LrgByteOrder  order)
{
    g_autoptr(GBytes) bytes = NULL;

    g_return_val_if_fail (data != NULL || length == 0, NULL);

    bytes = g_bytes_new (data, length);
    return lrg_byte_reader_new (bytes, order);
}

LrgByteReader *
lrg_byte_reader_ref (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_ref_count_inc (&self->ref_count);
    return self;
}

void
lrg_byte_reader_unref (LrgByteReader *self)
{
    g_return_if_fail (self != NULL);

    if (!g_atomic_ref_count_dec (&self->ref_count))
        return;
    g_bytes_unref (self->bytes);
    g_free (self);
}

GBytes *
lrg_byte_reader_get_bytes (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->bytes;
}

LrgByteOrder
lrg_byte_reader_get_byte_order (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    return self->order;
}

void
lrg_byte_reader_set_byte_order (LrgByteReader *self,
                                LrgByteOrder   order)
{
    g_return_if_fail (self != NULL);
    self->order = order;
}

gsize
lrg_byte_reader_get_size (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->size;
}

gsize
lrg_byte_reader_get_position (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->pos;
}

gsize
lrg_byte_reader_get_remaining (LrgByteReader *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->size - self->pos;
}

/*
 * need:
 * TRUE when @count bytes remain after the cursor; otherwise sets an EOF
 * error naming the position, so a parser's message says where the data
 * ran out.
 */
static gboolean
need (LrgByteReader  *self,
      gsize           count,
      GError        **error)
{
    if (count <= self->size - self->pos)
        return TRUE;

    g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_EOF,
                 "need %" G_GSIZE_FORMAT " bytes at offset %" G_GSIZE_FORMAT
                 ", only %" G_GSIZE_FORMAT " remain",
                 count, self->pos, self->size - self->pos);
    return FALSE;
}

gboolean
lrg_byte_reader_seek (LrgByteReader  *self,
                      gsize           position,
                      GError        **error)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (position > self->size)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE,
                     "offset %" G_GSIZE_FORMAT " is past the end (%" G_GSIZE_FORMAT " bytes)",
                     position, self->size);
        return FALSE;
    }
    self->pos = position;
    return TRUE;
}

gboolean
lrg_byte_reader_skip (LrgByteReader  *self,
                      gsize           count,
                      GError        **error)
{
    g_return_val_if_fail (self != NULL, FALSE);

    if (!need (self, count, error))
        return FALSE;
    self->pos += count;
    return TRUE;
}

gboolean
lrg_byte_reader_align (LrgByteReader  *self,
                       gsize           alignment,
                       GError        **error)
{
    gsize rem;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (alignment > 0 && (alignment & (alignment - 1)) == 0, FALSE);

    rem = self->pos & (alignment - 1);
    if (rem == 0)
        return TRUE;
    return lrg_byte_reader_skip (self, alignment - rem, error);
}

const guint8 *
lrg_byte_reader_peek_data (LrgByteReader  *self,
                           gsize           count,
                           GError        **error)
{
    g_return_val_if_fail (self != NULL, NULL);

    if (!need (self, count, error))
        return NULL;
    return self->data + self->pos;
}

const guint8 *
lrg_byte_reader_read_data (LrgByteReader  *self,
                           gsize           count,
                           GError        **error)
{
    const guint8 *p;

    p = lrg_byte_reader_peek_data (self, count, error);
    if (p != NULL)
        self->pos += count;
    return p;
}

GBytes *
lrg_byte_reader_read_bytes (LrgByteReader  *self,
                            gsize           count,
                            GError        **error)
{
    GBytes *slice;

    g_return_val_if_fail (self != NULL, NULL);

    if (!need (self, count, error))
        return NULL;
    slice = g_bytes_new_from_bytes (self->bytes, self->pos, count);
    self->pos += count;
    return slice;
}

gboolean
lrg_byte_reader_read_into (LrgByteReader  *self,
                           guint8         *buffer,
                           gsize           count,
                           GError        **error)
{
    const guint8 *p;

    g_return_val_if_fail (buffer != NULL || count == 0, FALSE);

    p = lrg_byte_reader_read_data (self, count, error);
    if (p == NULL)
        return FALSE;
    if (count > 0)
        memcpy (buffer, p, count);
    return TRUE;
}

/*
 * read_uint:
 * Assembles an unsigned value of @width bytes (1..8) in the reader's
 * byte order.  Byte-by-byte assembly is independent of the host's
 * endianness and alignment, which matters on aarch64 too.
 */
static gboolean
read_uint (LrgByteReader  *self,
           guint           width,
           guint64        *out,
           GError        **error)
{
    const guint8 *p;
    guint64 v;
    guint i;

    if (!need (self, width, error))
        return FALSE;

    p = self->data + self->pos;
    v = 0;
    if (self->order == LRG_BYTE_ORDER_LITTLE_ENDIAN)
    {
        for (i = width; i > 0; i--)
            v = (v << 8) | (guint64) p[i - 1];
    }
    else
    {
        for (i = 0; i < width; i++)
            v = (v << 8) | (guint64) p[i];
    }
    self->pos += width;
    *out = v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_u8 (LrgByteReader  *self,
                         guint8         *out_value,
                         GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 1, &v, error))
        return FALSE;
    *out_value = (guint8) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_i8 (LrgByteReader  *self,
                         gint8          *out_value,
                         GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 1, &v, error))
        return FALSE;
    *out_value = (gint8) (guint8) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_u16 (LrgByteReader  *self,
                          guint16        *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 2, &v, error))
        return FALSE;
    *out_value = (guint16) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_i16 (LrgByteReader  *self,
                          gint16         *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 2, &v, error))
        return FALSE;
    *out_value = (gint16) (guint16) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_u24 (LrgByteReader  *self,
                          guint32        *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 3, &v, error))
        return FALSE;
    *out_value = (guint32) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_u32 (LrgByteReader  *self,
                          guint32        *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 4, &v, error))
        return FALSE;
    *out_value = (guint32) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_i32 (LrgByteReader  *self,
                          gint32         *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 4, &v, error))
        return FALSE;
    *out_value = (gint32) (guint32) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_u64 (LrgByteReader  *self,
                          guint64        *out_value,
                          GError        **error)
{
    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    return read_uint (self, 8, out_value, error);
}

gboolean
lrg_byte_reader_read_i64 (LrgByteReader  *self,
                          gint64         *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 8, &v, error))
        return FALSE;
    *out_value = (gint64) v;
    return TRUE;
}

gboolean
lrg_byte_reader_read_f32 (LrgByteReader  *self,
                          gfloat         *out_value,
                          GError        **error)
{
    guint64 v;
    guint32 bits;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 4, &v, error))
        return FALSE;
    /* reinterpret the bits; memcpy avoids strict-aliasing trouble */
    bits = (guint32) v;
    memcpy (out_value, &bits, sizeof bits);
    return TRUE;
}

gboolean
lrg_byte_reader_read_f64 (LrgByteReader  *self,
                          gdouble        *out_value,
                          GError        **error)
{
    guint64 v;

    g_return_val_if_fail (self != NULL && out_value != NULL, FALSE);
    if (!read_uint (self, 8, &v, error))
        return FALSE;
    memcpy (out_value, &v, sizeof v);
    return TRUE;
}

gboolean
lrg_byte_reader_read_fourcc (LrgByteReader  *self,
                             guint32        *out_value,
                             GError        **error)
{
    return lrg_byte_reader_read_u32 (self, out_value, error);
}

gboolean
lrg_byte_reader_expect_u32 (LrgByteReader  *self,
                            guint32         expected,
                            GError        **error)
{
    gsize start;
    guint32 v;

    g_return_val_if_fail (self != NULL, FALSE);

    start = self->pos;
    if (!lrg_byte_reader_read_u32 (self, &v, error))
        return FALSE;
    if (v != expected)
    {
        self->pos = start;
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID,
                     "expected 0x%08x at offset %" G_GSIZE_FORMAT ", found 0x%08x",
                     expected, start, v);
        return FALSE;
    }
    return TRUE;
}

/*
 * cstring_length:
 * The length of the NUL-terminated string at @offset, or -1 when no NUL
 * occurs within @max_length (0: up to the end).
 */
static gssize
cstring_length (LrgByteReader *self,
                gsize          offset,
                gsize          max_length)
{
    const guint8 *start;
    const guint8 *nul;
    gsize avail;

    if (offset >= self->size)
        return -1;
    avail = self->size - offset;
    /* the NUL itself must fall within max_length + 1 bytes */
    if (max_length > 0 && max_length < avail)
        avail = max_length + 1;
    start = self->data + offset;
    nul = memchr (start, '\0', avail);
    if (nul == NULL)
        return -1;
    return (gssize) (nul - start);
}

gchar *
lrg_byte_reader_read_cstring (LrgByteReader  *self,
                              gsize           max_length,
                              GError        **error)
{
    gssize len;
    gchar *s;

    g_return_val_if_fail (self != NULL, NULL);

    len = cstring_length (self, self->pos, max_length);
    if (len < 0)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID,
                     "unterminated string at offset %" G_GSIZE_FORMAT, self->pos);
        return NULL;
    }
    s = g_strndup ((const gchar *) (self->data + self->pos), (gsize) len);
    self->pos += (gsize) len + 1;
    return s;
}

gchar *
lrg_byte_reader_read_fixed_string (LrgByteReader  *self,
                                   gsize           length,
                                   GError        **error)
{
    const guint8 *p;
    const guint8 *nul;

    p = lrg_byte_reader_read_data (self, length, error);
    if (p == NULL)
        return NULL;
    nul = length > 0 ? memchr (p, '\0', length) : NULL;
    return g_strndup ((const gchar *) p, nul != NULL ? (gsize) (nul - p) : length);
}

gchar *
lrg_byte_reader_get_cstring_at (LrgByteReader  *self,
                                gsize           offset,
                                GError        **error)
{
    gssize len;

    g_return_val_if_fail (self != NULL, NULL);

    if (offset >= self->size)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE,
                     "string offset %" G_GSIZE_FORMAT " is past the end (%" G_GSIZE_FORMAT " bytes)",
                     offset, self->size);
        return NULL;
    }
    len = cstring_length (self, offset, 0);
    if (len < 0)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_INVALID,
                     "unterminated string at offset %" G_GSIZE_FORMAT, offset);
        return NULL;
    }
    return g_strndup ((const gchar *) (self->data + offset), (gsize) len);
}

LrgByteReader *
lrg_byte_reader_new_sub_reader (LrgByteReader  *self,
                                gsize           offset,
                                gsize           length,
                                GError        **error)
{
    g_autoptr(GBytes) slice = NULL;

    g_return_val_if_fail (self != NULL, NULL);

    /* written to avoid offset + length overflowing */
    if (offset > self->size || length > self->size - offset)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE,
                     "slice %" G_GSIZE_FORMAT "+%" G_GSIZE_FORMAT
                     " lies outside %" G_GSIZE_FORMAT " bytes",
                     offset, length, self->size);
        return NULL;
    }
    slice = g_bytes_new_from_bytes (self->bytes, offset, length);
    return lrg_byte_reader_new (slice, self->order);
}

LrgByteReader *
lrg_byte_reader_read_sub_reader (LrgByteReader  *self,
                                 gsize           length,
                                 GError        **error)
{
    LrgByteReader *sub;

    g_return_val_if_fail (self != NULL, NULL);

    if (!need (self, length, error))
        return NULL;
    sub = lrg_byte_reader_new_sub_reader (self, self->pos, length, error);
    if (sub != NULL)
        self->pos += length;
    return sub;
}
