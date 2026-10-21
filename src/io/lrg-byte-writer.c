/* lrg-byte-writer.c - Building binary data
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"
#include "lrg-byte-writer.h"

#include <string.h>

struct _LrgByteWriter
{
    gatomicrefcount  ref_count;
    GByteArray      *buffer;
    LrgByteOrder     order;
};

G_DEFINE_BOXED_TYPE (LrgByteWriter, lrg_byte_writer, lrg_byte_writer_ref, lrg_byte_writer_unref)

LrgByteWriter *
lrg_byte_writer_new (LrgByteOrder order)
{
    LrgByteWriter *self;

    self = g_new0 (LrgByteWriter, 1);
    g_atomic_ref_count_init (&self->ref_count);
    self->buffer = g_byte_array_new ();
    self->order = order;
    return self;
}

LrgByteWriter *
lrg_byte_writer_ref (LrgByteWriter *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_ref_count_inc (&self->ref_count);
    return self;
}

void
lrg_byte_writer_unref (LrgByteWriter *self)
{
    g_return_if_fail (self != NULL);

    if (!g_atomic_ref_count_dec (&self->ref_count))
        return;
    g_byte_array_unref (self->buffer);
    g_free (self);
}

LrgByteOrder
lrg_byte_writer_get_byte_order (LrgByteWriter *self)
{
    g_return_val_if_fail (self != NULL, LRG_BYTE_ORDER_LITTLE_ENDIAN);
    return self->order;
}

void
lrg_byte_writer_set_byte_order (LrgByteWriter *self,
                                LrgByteOrder   order)
{
    g_return_if_fail (self != NULL);
    self->order = order;
}

gsize
lrg_byte_writer_get_size (LrgByteWriter *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->buffer->len;
}

const guint8 *
lrg_byte_writer_get_data (LrgByteWriter *self,
                          gsize         *out_length)
{
    g_return_val_if_fail (self != NULL, NULL);

    if (out_length != NULL)
        *out_length = self->buffer->len;
    return self->buffer->data;
}

GBytes *
lrg_byte_writer_to_bytes (LrgByteWriter *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return g_bytes_new (self->buffer->data, self->buffer->len);
}

GBytes *
lrg_byte_writer_steal_bytes (LrgByteWriter *self)
{
    GBytes *bytes;

    g_return_val_if_fail (self != NULL, NULL);

    /* g_byte_array_free_to_bytes consumes the array; start a new one */
    bytes = g_byte_array_free_to_bytes (self->buffer);
    self->buffer = g_byte_array_new ();
    return bytes;
}

void
lrg_byte_writer_clear (LrgByteWriter *self)
{
    g_return_if_fail (self != NULL);
    g_byte_array_set_size (self->buffer, 0);
}

void
lrg_byte_writer_write_data (LrgByteWriter *self,
                            const guint8  *data,
                            gsize          length)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (data != NULL || length == 0);

    if (length > 0)
        g_byte_array_append (self->buffer, data, (guint) length);
}

void
lrg_byte_writer_write_bytes (LrgByteWriter *self,
                             GBytes        *bytes)
{
    const guint8 *data;
    gsize length;

    g_return_if_fail (self != NULL);
    g_return_if_fail (bytes != NULL);

    data = g_bytes_get_data (bytes, &length);
    lrg_byte_writer_write_data (self, data, length);
}

void
lrg_byte_writer_write_zeros (LrgByteWriter *self,
                             gsize          count)
{
    guint old;

    g_return_if_fail (self != NULL);

    old = self->buffer->len;
    g_byte_array_set_size (self->buffer, old + (guint) count);
    memset (self->buffer->data + old, 0, count);
}

/*
 * encode_uint:
 * The low @width bytes of @value in @order into @out.
 */
static void
encode_uint (guint8       *out,
             guint64       value,
             guint         width,
             LrgByteOrder  order)
{
    guint i;

    for (i = 0; i < width; i++)
    {
        guint8 b = (guint8) ((value >> (8 * i)) & 0xFF);

        if (order == LRG_BYTE_ORDER_LITTLE_ENDIAN)
            out[i] = b;
        else
            out[width - 1 - i] = b;
    }
}

static void
write_uint (LrgByteWriter *self,
            guint64        value,
            guint          width)
{
    guint8 tmp[8];

    encode_uint (tmp, value, width, self->order);
    g_byte_array_append (self->buffer, tmp, width);
}

void
lrg_byte_writer_write_u8 (LrgByteWriter *self,
                          guint8         value)
{
    g_return_if_fail (self != NULL);
    write_uint (self, value, 1);
}

void
lrg_byte_writer_write_u16 (LrgByteWriter *self,
                           guint16        value)
{
    g_return_if_fail (self != NULL);
    write_uint (self, value, 2);
}

void
lrg_byte_writer_write_u24 (LrgByteWriter *self,
                           guint32        value)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (value <= 0xFFFFFFu);
    write_uint (self, value, 3);
}

void
lrg_byte_writer_write_u32 (LrgByteWriter *self,
                           guint32        value)
{
    g_return_if_fail (self != NULL);
    write_uint (self, value, 4);
}

void
lrg_byte_writer_write_u64 (LrgByteWriter *self,
                           guint64        value)
{
    g_return_if_fail (self != NULL);
    write_uint (self, value, 8);
}

void
lrg_byte_writer_write_i32 (LrgByteWriter *self,
                           gint32         value)
{
    g_return_if_fail (self != NULL);
    write_uint (self, (guint32) value, 4);
}

void
lrg_byte_writer_write_f32 (LrgByteWriter *self,
                           gfloat         value)
{
    guint32 bits;

    g_return_if_fail (self != NULL);
    memcpy (&bits, &value, sizeof bits);
    write_uint (self, bits, 4);
}

void
lrg_byte_writer_write_f64 (LrgByteWriter *self,
                           gdouble        value)
{
    guint64 bits;

    g_return_if_fail (self != NULL);
    memcpy (&bits, &value, sizeof bits);
    write_uint (self, bits, 8);
}

void
lrg_byte_writer_write_cstring (LrgByteWriter *self,
                               const gchar   *value)
{
    g_return_if_fail (self != NULL);

    if (value != NULL)
        lrg_byte_writer_write_data (self, (const guint8 *) value, strlen (value));
    lrg_byte_writer_write_u8 (self, 0);
}

void
lrg_byte_writer_write_fixed_string (LrgByteWriter *self,
                                    const gchar   *value,
                                    gsize          length)
{
    gsize len;

    g_return_if_fail (self != NULL);

    len = value != NULL ? strlen (value) : 0;
    if (len > length)
        len = length;
    lrg_byte_writer_write_data (self, (const guint8 *) value, len);
    lrg_byte_writer_write_zeros (self, length - len);
}

/* one place for the bounds check of the patch calls */
static gboolean
patch_uint (LrgByteWriter  *self,
            gsize           position,
            guint64         value,
            guint           width,
            GError        **error)
{
    if (position > self->buffer->len || width > self->buffer->len - position)
    {
        g_set_error (error, LRG_BYTE_ERROR, LRG_BYTE_ERROR_RANGE,
                     "cannot patch %u bytes at offset %" G_GSIZE_FORMAT " of %u written",
                     width, position, self->buffer->len);
        return FALSE;
    }
    encode_uint (self->buffer->data + position, value, width, self->order);
    return TRUE;
}

gboolean
lrg_byte_writer_patch_u16 (LrgByteWriter  *self,
                           gsize           position,
                           guint16         value,
                           GError        **error)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return patch_uint (self, position, value, 2, error);
}

gboolean
lrg_byte_writer_patch_u32 (LrgByteWriter  *self,
                           gsize           position,
                           guint32         value,
                           GError        **error)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return patch_uint (self, position, value, 4, error);
}
