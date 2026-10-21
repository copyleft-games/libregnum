/* lrg-byte-writer.h - Building binary data
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * The counterpart of #LrgByteReader: appends integers, floats, strings
 * and raw bytes in a chosen byte order to a growing buffer, for network
 * packets, save files and test fixtures.  Values written at a position
 * already written (a length field filled in afterwards) go through the
 * lrg_byte_writer_patch_*() calls.
 */

#ifndef LRG_BYTE_WRITER_H
#define LRG_BYTE_WRITER_H

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-byte-reader.h"

G_BEGIN_DECLS

#define LRG_TYPE_BYTE_WRITER (lrg_byte_writer_get_type ())

/**
 * LrgByteWriter:
 *
 * A reference-counted, growable output buffer.  Not thread-safe.
 */
typedef struct _LrgByteWriter LrgByteWriter;

LRG_AVAILABLE_IN_ALL
GType lrg_byte_writer_get_type (void) G_GNUC_CONST;

/**
 * lrg_byte_writer_new:
 * @order: the byte order of multi-byte writes
 *
 * Returns: (transfer full): an empty writer
 */
LRG_AVAILABLE_IN_ALL
LrgByteWriter *lrg_byte_writer_new (LrgByteOrder order);

/**
 * lrg_byte_writer_ref:
 * @self: an #LrgByteWriter
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgByteWriter *lrg_byte_writer_ref (LrgByteWriter *self);

/**
 * lrg_byte_writer_unref:
 * @self: (transfer full): an #LrgByteWriter
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_unref (LrgByteWriter *self);

/**
 * lrg_byte_writer_get_byte_order:
 * @self: an #LrgByteWriter
 *
 * Returns: the byte order of multi-byte writes
 */
LRG_AVAILABLE_IN_ALL
LrgByteOrder lrg_byte_writer_get_byte_order (LrgByteWriter *self);

/**
 * lrg_byte_writer_set_byte_order:
 * @self: an #LrgByteWriter
 * @order: the new byte order
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_set_byte_order (LrgByteWriter *self,
                                     LrgByteOrder   order);

/**
 * lrg_byte_writer_get_size:
 * @self: an #LrgByteWriter
 *
 * Returns: the bytes written so far
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_byte_writer_get_size (LrgByteWriter *self);

/**
 * lrg_byte_writer_get_data:
 * @self: an #LrgByteWriter
 * @out_length: (out) (optional): the length
 *
 * Borrows the buffer; valid until the next write.
 *
 * Returns: (transfer none) (array length=out_length): the bytes
 */
LRG_AVAILABLE_IN_ALL
const guint8 *lrg_byte_writer_get_data (LrgByteWriter *self,
                                        gsize         *out_length);

/**
 * lrg_byte_writer_to_bytes:
 * @self: an #LrgByteWriter
 *
 * Copies the buffer; the writer stays usable.
 *
 * Returns: (transfer full): the bytes written so far
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_byte_writer_to_bytes (LrgByteWriter *self);

/**
 * lrg_byte_writer_steal_bytes:
 * @self: an #LrgByteWriter
 *
 * Hands the buffer over without copying; the writer is empty afterwards.
 *
 * Returns: (transfer full): the bytes written so far
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_byte_writer_steal_bytes (LrgByteWriter *self);

/**
 * lrg_byte_writer_clear:
 * @self: an #LrgByteWriter
 *
 * Empties the buffer and keeps its allocation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_clear (LrgByteWriter *self);

/**
 * lrg_byte_writer_write_data:
 * @self: an #LrgByteWriter
 * @data: (array length=length) (element-type guint8): bytes to append
 * @length: the length of @data
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_data (LrgByteWriter *self,
                                 const guint8  *data,
                                 gsize          length);

/**
 * lrg_byte_writer_write_bytes:
 * @self: an #LrgByteWriter
 * @bytes: bytes to append
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_bytes (LrgByteWriter *self,
                                  GBytes        *bytes);

/**
 * lrg_byte_writer_write_zeros:
 * @self: an #LrgByteWriter
 * @count: how many zero bytes to append
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_zeros (LrgByteWriter *self,
                                  gsize          count);

/**
 * lrg_byte_writer_write_u8:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_u8 (LrgByteWriter *self,
                               guint8         value);

/**
 * lrg_byte_writer_write_u16:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_u16 (LrgByteWriter *self,
                                guint16        value);

/**
 * lrg_byte_writer_write_u24:
 * @self: an #LrgByteWriter
 * @value: the value; bits above 24 must be zero
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_u24 (LrgByteWriter *self,
                                guint32        value);

/**
 * lrg_byte_writer_write_u32:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_u32 (LrgByteWriter *self,
                                guint32        value);

/**
 * lrg_byte_writer_write_u64:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_u64 (LrgByteWriter *self,
                                guint64        value);

/**
 * lrg_byte_writer_write_i32:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_i32 (LrgByteWriter *self,
                                gint32         value);

/**
 * lrg_byte_writer_write_f32:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_f32 (LrgByteWriter *self,
                                gfloat         value);

/**
 * lrg_byte_writer_write_f64:
 * @self: an #LrgByteWriter
 * @value: the value
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_f64 (LrgByteWriter *self,
                                gdouble        value);

/**
 * lrg_byte_writer_write_cstring:
 * @self: an #LrgByteWriter
 * @value: (nullable): the string; %NULL writes an empty string
 *
 * Appends the string and its terminating NUL.
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_cstring (LrgByteWriter *self,
                                    const gchar   *value);

/**
 * lrg_byte_writer_write_fixed_string:
 * @self: an #LrgByteWriter
 * @value: (nullable): the string
 * @length: the field's width; the string is truncated or NUL-padded
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_writer_write_fixed_string (LrgByteWriter *self,
                                         const gchar   *value,
                                         gsize          length);

/**
 * lrg_byte_writer_patch_u16:
 * @self: an #LrgByteWriter
 * @position: where an earlier write put the field
 * @value: the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; %LRG_BYTE_ERROR_RANGE when the field does
 *   not lie inside what was written
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_writer_patch_u16 (LrgByteWriter  *self,
                                    gsize           position,
                                    guint16         value,
                                    GError        **error);

/**
 * lrg_byte_writer_patch_u32:
 * @self: an #LrgByteWriter
 * @position: where an earlier write put the field
 * @value: the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_writer_patch_u32 (LrgByteWriter  *self,
                                    gsize           position,
                                    guint32         value,
                                    GError        **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgByteWriter, lrg_byte_writer_unref)

G_END_DECLS

#endif /* LRG_BYTE_WRITER_H */
