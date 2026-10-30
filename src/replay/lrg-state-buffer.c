/* lrg-state-buffer.c - Binary buffer for simulation state and checksums
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-state-buffer.h"

#include <gio/gio.h>
#include <string.h>

/* 64-bit FNV-1a parameters */
#define LRG_FNV64_OFFSET_BASIS (G_GUINT64_CONSTANT (14695981039346656037))
#define LRG_FNV64_PRIME        (G_GUINT64_CONSTANT (1099511628211))

/* Length marker that stands for a NULL string */
#define LRG_STATE_NULL_STRING  (G_MAXUINT32)

struct _LrgStateBuffer
{
    GObject     parent_instance;

    GByteArray *data;      /* written bytes (owned) */
    gsize       position;  /* read cursor */
    gboolean    overrun;   /* a read ran past the end */
};

G_DEFINE_FINAL_TYPE (LrgStateBuffer, lrg_state_buffer, G_TYPE_OBJECT)

static void
lrg_state_buffer_finalize (GObject *object)
{
    LrgStateBuffer *self = LRG_STATE_BUFFER (object);

    g_clear_pointer (&self->data, g_byte_array_unref);

    G_OBJECT_CLASS (lrg_state_buffer_parent_class)->finalize (object);
}

static void
lrg_state_buffer_class_init (LrgStateBufferClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_state_buffer_finalize;
}

static void
lrg_state_buffer_init (LrgStateBuffer *self)
{
    self->data = g_byte_array_new ();
    self->position = 0;
    self->overrun = FALSE;
}

LrgStateBuffer *
lrg_state_buffer_new (void)
{
    return g_object_new (LRG_TYPE_STATE_BUFFER, NULL);
}

LrgStateBuffer *
lrg_state_buffer_new_from_data (const guint8 *data,
                                gsize         size)
{
    LrgStateBuffer *self;

    g_return_val_if_fail (data != NULL || size == 0, NULL);

    self = lrg_state_buffer_new ();
    if (size > 0)
        g_byte_array_append (self->data, data, (guint) size);
    return self;
}

LrgStateBuffer *
lrg_state_buffer_new_from_bytes (GBytes *bytes)
{
    gconstpointer data;
    gsize size;

    g_return_val_if_fail (bytes != NULL, NULL);

    data = g_bytes_get_data (bytes, &size);
    return lrg_state_buffer_new_from_data ((const guint8 *) data, size);
}

void
lrg_state_buffer_reset (LrgStateBuffer *self)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    g_byte_array_set_size (self->data, 0);
    self->position = 0;
    self->overrun = FALSE;
}

void
lrg_state_buffer_rewind (LrgStateBuffer *self)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    self->position = 0;
    self->overrun = FALSE;
}

gsize
lrg_state_buffer_get_size (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return self->data->len;
}

gsize
lrg_state_buffer_get_position (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return self->position;
}

gsize
lrg_state_buffer_get_remaining (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return self->data->len - MIN (self->position, (gsize) self->data->len);
}

const guint8 *
lrg_state_buffer_get_data (LrgStateBuffer *self,
                           gsize          *size)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), NULL);

    if (size != NULL)
        *size = self->data->len;
    return self->data->data;
}

GBytes *
lrg_state_buffer_to_bytes (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), NULL);

    return g_bytes_new (self->data->data, self->data->len);
}

guint64
lrg_state_hash_data (const guint8 *data,
                     gsize         size,
                     guint64       seed)
{
    guint64 hash;
    gsize i;

    /* FNV-1a: xor the byte in, then multiply; byte-at-a-time so the result
     * does not depend on alignment or endianness */
    hash = seed != 0 ? seed : LRG_FNV64_OFFSET_BASIS;
    for (i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= LRG_FNV64_PRIME;
    }
    return hash;
}

guint64
lrg_state_buffer_checksum (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return lrg_state_hash_data (self->data->data, self->data->len, 0);
}

/* ==========================================================================
 * Writers: everything is serialized little-endian, byte by byte
 * ========================================================================== */

static void
append_le (LrgStateBuffer *self,
           guint64         value,
           guint           n_bytes)
{
    guint8 bytes[8];
    guint i;

    for (i = 0; i < n_bytes; i++)
        bytes[i] = (guint8) (value >> (8 * i));
    g_byte_array_append (self->data, bytes, n_bytes);
}

void
lrg_state_buffer_write_uint8 (LrgStateBuffer *self,
                              guint8          value)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    g_byte_array_append (self->data, &value, 1);
}

void
lrg_state_buffer_write_boolean (LrgStateBuffer *self,
                                gboolean        value)
{
    lrg_state_buffer_write_uint8 (self, value ? 1 : 0);
}

void
lrg_state_buffer_write_uint32 (LrgStateBuffer *self,
                               guint32         value)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    append_le (self, value, 4);
}

void
lrg_state_buffer_write_int32 (LrgStateBuffer *self,
                              gint32          value)
{
    lrg_state_buffer_write_uint32 (self, (guint32) value);
}

void
lrg_state_buffer_write_uint64 (LrgStateBuffer *self,
                               guint64         value)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    append_le (self, value, 8);
}

void
lrg_state_buffer_write_int64 (LrgStateBuffer *self,
                              gint64          value)
{
    lrg_state_buffer_write_uint64 (self, (guint64) value);
}

void
lrg_state_buffer_write_float (LrgStateBuffer *self,
                              gfloat          value)
{
    guint32 bits;

    /* memcpy is the strict-aliasing-safe way to take the bit pattern */
    memcpy (&bits, &value, sizeof (bits));
    lrg_state_buffer_write_uint32 (self, bits);
}

void
lrg_state_buffer_write_double (LrgStateBuffer *self,
                               gdouble         value)
{
    guint64 bits;

    memcpy (&bits, &value, sizeof (bits));
    lrg_state_buffer_write_uint64 (self, bits);
}

void
lrg_state_buffer_write_data (LrgStateBuffer *self,
                             const guint8   *data,
                             gsize           size)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));
    g_return_if_fail (data != NULL || size == 0);
    g_return_if_fail (size < LRG_STATE_NULL_STRING);

    append_le (self, size, 4);
    if (size > 0)
        g_byte_array_append (self->data, data, (guint) size);
}

void
lrg_state_buffer_write_string (LrgStateBuffer *self,
                               const gchar    *value)
{
    g_return_if_fail (LRG_IS_STATE_BUFFER (self));

    if (value == NULL)
    {
        append_le (self, LRG_STATE_NULL_STRING, 4);
        return;
    }
    lrg_state_buffer_write_data (self, (const guint8 *) value, strlen (value));
}

/* ==========================================================================
 * Readers: a read past the end yields zero and latches the overrun flag
 * ========================================================================== */

static gboolean
take (LrgStateBuffer *self,
      gsize           n_bytes,
      const guint8  **out)
{
    if (self->overrun || n_bytes > lrg_state_buffer_get_remaining (self))
    {
        self->overrun = TRUE;
        *out = NULL;
        return FALSE;
    }
    *out = self->data->data + self->position;
    self->position += n_bytes;
    return TRUE;
}

static guint64
read_le (LrgStateBuffer *self,
         guint           n_bytes)
{
    const guint8 *bytes;
    guint64 value;
    guint i;

    if (!take (self, n_bytes, &bytes))
        return 0;
    value = 0;
    for (i = 0; i < n_bytes; i++)
        value |= ((guint64) bytes[i]) << (8 * i);
    return value;
}

guint8
lrg_state_buffer_read_uint8 (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return (guint8) read_le (self, 1);
}

gboolean
lrg_state_buffer_read_boolean (LrgStateBuffer *self)
{
    return lrg_state_buffer_read_uint8 (self) != 0;
}

guint32
lrg_state_buffer_read_uint32 (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return (guint32) read_le (self, 4);
}

gint32
lrg_state_buffer_read_int32 (LrgStateBuffer *self)
{
    return (gint32) lrg_state_buffer_read_uint32 (self);
}

guint64
lrg_state_buffer_read_uint64 (LrgStateBuffer *self)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), 0);

    return read_le (self, 8);
}

gint64
lrg_state_buffer_read_int64 (LrgStateBuffer *self)
{
    return (gint64) lrg_state_buffer_read_uint64 (self);
}

gfloat
lrg_state_buffer_read_float (LrgStateBuffer *self)
{
    guint32 bits;
    gfloat value;

    bits = lrg_state_buffer_read_uint32 (self);
    memcpy (&value, &bits, sizeof (value));
    return value;
}

gdouble
lrg_state_buffer_read_double (LrgStateBuffer *self)
{
    guint64 bits;
    gdouble value;

    bits = lrg_state_buffer_read_uint64 (self);
    memcpy (&value, &bits, sizeof (value));
    return value;
}

gchar *
lrg_state_buffer_read_string (LrgStateBuffer *self)
{
    const guint8 *bytes;
    guint32 length;

    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), NULL);

    length = (guint32) read_le (self, 4);
    if (self->overrun || length == LRG_STATE_NULL_STRING)
        return NULL;
    if (!take (self, length, &bytes))
        return NULL;
    return g_strndup ((const gchar *) bytes, length);
}

GBytes *
lrg_state_buffer_read_data (LrgStateBuffer *self)
{
    const guint8 *bytes;
    guint32 length;

    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), NULL);

    length = (guint32) read_le (self, 4);
    if (self->overrun || length == LRG_STATE_NULL_STRING)
        return NULL;
    if (!take (self, length, &bytes))
        return NULL;
    return g_bytes_new (bytes, length);
}

gboolean
lrg_state_buffer_check (LrgStateBuffer  *self,
                        GError         **error)
{
    g_return_val_if_fail (LRG_IS_STATE_BUFFER (self), FALSE);

    if (!self->overrun)
        return TRUE;
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                         "State buffer read past its end (truncated or mismatched state)");
    return FALSE;
}
