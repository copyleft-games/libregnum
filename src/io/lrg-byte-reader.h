/* lrg-byte-reader.h - Bounds-checked reading of binary data
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * Game file formats and network protocols are sequences of little- or
 * big-endian integers, floats, strings and sub-blocks.  An
 * #LrgByteReader is a cursor over a #GBytes that reads them without
 * ever running past the end: every read either succeeds or returns
 * %FALSE with an %LRG_BYTE_ERROR, leaving the cursor where it was, so a
 * parser fed truncated or hostile data fails cleanly instead of reading
 * out of bounds.
 */

#ifndef LRG_BYTE_READER_H
#define LRG_BYTE_READER_H

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_BYTE_ERROR:
 *
 * Error domain for #LrgByteReader and #LrgByteWriter.  Errors carry an
 * #LrgByteError code.
 */
#define LRG_BYTE_ERROR (lrg_byte_error_quark ())

/**
 * LrgByteError:
 * @LRG_BYTE_ERROR_EOF: the read needs more bytes than remain
 * @LRG_BYTE_ERROR_RANGE: a position or length lies outside the data
 * @LRG_BYTE_ERROR_INVALID: the data is not what the read expected (an
 *   unterminated string, a magic number that does not match)
 *
 * Error codes for the %LRG_BYTE_ERROR domain.
 */
typedef enum
{
    LRG_BYTE_ERROR_EOF,
    LRG_BYTE_ERROR_RANGE,
    LRG_BYTE_ERROR_INVALID
} LrgByteError;

LRG_AVAILABLE_IN_ALL
GQuark lrg_byte_error_quark (void);

/**
 * LrgByteOrder:
 * @LRG_BYTE_ORDER_LITTLE_ENDIAN: least significant byte first
 * @LRG_BYTE_ORDER_BIG_ENDIAN: most significant byte first (network order)
 *
 * The byte order multi-byte values are read and written in.
 */
typedef enum
{
    LRG_BYTE_ORDER_LITTLE_ENDIAN,
    LRG_BYTE_ORDER_BIG_ENDIAN
} LrgByteOrder;

LRG_AVAILABLE_IN_ALL
GType lrg_byte_order_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_BYTE_ORDER (lrg_byte_order_get_type ())

#define LRG_TYPE_BYTE_READER (lrg_byte_reader_get_type ())

/**
 * LrgByteReader:
 *
 * A reference-counted cursor over immutable bytes.  The reader holds a
 * reference to its #GBytes; data returned by
 * lrg_byte_reader_read_bytes() and sub-readers share it without
 * copying.  A reader is not thread-safe; give each thread its own
 * (lrg_byte_reader_new() on the same #GBytes is cheap).
 */
typedef struct _LrgByteReader LrgByteReader;

LRG_AVAILABLE_IN_ALL
GType lrg_byte_reader_get_type (void) G_GNUC_CONST;

/**
 * lrg_byte_reader_new:
 * @bytes: the data to read
 * @order: the byte order of multi-byte reads
 *
 * Returns: (transfer full): a reader positioned at offset 0
 */
LRG_AVAILABLE_IN_ALL
LrgByteReader *lrg_byte_reader_new (GBytes       *bytes,
                                    LrgByteOrder  order);

/**
 * lrg_byte_reader_new_from_data:
 * @data: (array length=length) (element-type guint8): the data
 * @length: the length of @data
 * @order: the byte order of multi-byte reads
 *
 * Copies @data into a new #GBytes.
 *
 * Returns: (transfer full): a reader positioned at offset 0
 */
LRG_AVAILABLE_IN_ALL
LrgByteReader *lrg_byte_reader_new_from_data (const guint8 *data,
                                              gsize         length,
                                              LrgByteOrder  order);

/**
 * lrg_byte_reader_ref:
 * @self: an #LrgByteReader
 *
 * Returns: (transfer full): @self
 */
LRG_AVAILABLE_IN_ALL
LrgByteReader *lrg_byte_reader_ref (LrgByteReader *self);

/**
 * lrg_byte_reader_unref:
 * @self: (transfer full): an #LrgByteReader
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_reader_unref (LrgByteReader *self);

/**
 * lrg_byte_reader_get_bytes:
 * @self: an #LrgByteReader
 *
 * Returns: (transfer none): the whole data the reader reads
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_byte_reader_get_bytes (LrgByteReader *self);

/**
 * lrg_byte_reader_get_byte_order:
 * @self: an #LrgByteReader
 *
 * Returns: the byte order of multi-byte reads
 */
LRG_AVAILABLE_IN_ALL
LrgByteOrder lrg_byte_reader_get_byte_order (LrgByteReader *self);

/**
 * lrg_byte_reader_set_byte_order:
 * @self: an #LrgByteReader
 * @order: the new byte order
 *
 * Formats that mix orders switch per field.
 */
LRG_AVAILABLE_IN_ALL
void lrg_byte_reader_set_byte_order (LrgByteReader *self,
                                     LrgByteOrder   order);

/**
 * lrg_byte_reader_get_size:
 * @self: an #LrgByteReader
 *
 * Returns: the length of the data
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_byte_reader_get_size (LrgByteReader *self);

/**
 * lrg_byte_reader_get_position:
 * @self: an #LrgByteReader
 *
 * Returns: the cursor's offset from the start
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_byte_reader_get_position (LrgByteReader *self);

/**
 * lrg_byte_reader_get_remaining:
 * @self: an #LrgByteReader
 *
 * Returns: the bytes left after the cursor
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_byte_reader_get_remaining (LrgByteReader *self);

/**
 * lrg_byte_reader_seek:
 * @self: an #LrgByteReader
 * @position: the new offset; equal to the size is allowed (the end)
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; %LRG_BYTE_ERROR_RANGE past the end
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_seek (LrgByteReader  *self,
                               gsize           position,
                               GError        **error);

/**
 * lrg_byte_reader_skip:
 * @self: an #LrgByteReader
 * @count: bytes to skip
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; %LRG_BYTE_ERROR_EOF when fewer remain
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_skip (LrgByteReader  *self,
                               gsize           count,
                               GError        **error);

/**
 * lrg_byte_reader_align:
 * @self: an #LrgByteReader
 * @alignment: a power of two
 * @error: return location for a #GError
 *
 * Moves the cursor forward to the next multiple of @alignment.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_align (LrgByteReader  *self,
                                gsize           alignment,
                                GError        **error);

/**
 * lrg_byte_reader_peek_data:
 * @self: an #LrgByteReader
 * @count: bytes wanted
 * @error: return location for a #GError
 *
 * Borrows the next @count bytes without moving the cursor; the pointer
 * stays valid while the reader (or its #GBytes) lives.
 *
 * Returns: (transfer none) (nullable) (array length=count): the bytes,
 *   or %NULL with %LRG_BYTE_ERROR_EOF
 */
LRG_AVAILABLE_IN_ALL
const guint8 *lrg_byte_reader_peek_data (LrgByteReader  *self,
                                         gsize           count,
                                         GError        **error);

/**
 * lrg_byte_reader_read_data:
 * @self: an #LrgByteReader
 * @count: bytes wanted
 * @error: return location for a #GError
 *
 * Like lrg_byte_reader_peek_data(), then moves the cursor past them.
 *
 * Returns: (transfer none) (nullable) (array length=count): the bytes
 */
LRG_AVAILABLE_IN_ALL
const guint8 *lrg_byte_reader_read_data (LrgByteReader  *self,
                                         gsize           count,
                                         GError        **error);

/**
 * lrg_byte_reader_read_bytes:
 * @self: an #LrgByteReader
 * @count: bytes wanted
 * @error: return location for a #GError
 *
 * Returns: (transfer full) (nullable): the next @count bytes, sharing the
 *   reader's data without copying
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_byte_reader_read_bytes (LrgByteReader  *self,
                                    gsize           count,
                                    GError        **error);

/**
 * lrg_byte_reader_read_into:
 * @self: an #LrgByteReader
 * @buffer: (array length=count) (element-type guint8) (out caller-allocates):
 *   where to copy
 * @count: bytes wanted
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_into (LrgByteReader  *self,
                                    guint8         *buffer,
                                    gsize           count,
                                    GError        **error);

/**
 * lrg_byte_reader_read_u8:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success; on failure @out_value and the cursor are
 *   unchanged
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_u8 (LrgByteReader  *self,
                                  guint8         *out_value,
                                  GError        **error);

/**
 * lrg_byte_reader_read_i8:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_i8 (LrgByteReader  *self,
                                  gint8          *out_value,
                                  GError        **error);

/**
 * lrg_byte_reader_read_u16:
 * @self: an #LrgByteReader
 * @out_value: (out): the value, in the reader's byte order
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_u16 (LrgByteReader  *self,
                                   guint16        *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_i16:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_i16 (LrgByteReader  *self,
                                   gint16         *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_u24:
 * @self: an #LrgByteReader
 * @out_value: (out): the 24-bit value, zero-extended
 * @error: return location for a #GError
 *
 * Network protocols and packed formats use three-byte lengths.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_u24 (LrgByteReader  *self,
                                   guint32        *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_u32:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_u32 (LrgByteReader  *self,
                                   guint32        *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_i32:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_i32 (LrgByteReader  *self,
                                   gint32         *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_u64:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_u64 (LrgByteReader  *self,
                                   guint64        *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_i64:
 * @self: an #LrgByteReader
 * @out_value: (out): the value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_i64 (LrgByteReader  *self,
                                   gint64         *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_f32:
 * @self: an #LrgByteReader
 * @out_value: (out): the IEEE-754 single-precision value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_f32 (LrgByteReader  *self,
                                   gfloat         *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_f64:
 * @self: an #LrgByteReader
 * @out_value: (out): the IEEE-754 double-precision value
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_f64 (LrgByteReader  *self,
                                   gdouble        *out_value,
                                   GError        **error);

/**
 * lrg_byte_reader_read_fourcc:
 * @self: an #LrgByteReader
 * @out_value: (out): the four bytes as a 32-bit value in the reader's
 *   byte order (so a little-endian "REVM" chunk tag reads as 'MVER')
 * @error: return location for a #GError
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_read_fourcc (LrgByteReader  *self,
                                      guint32        *out_value,
                                      GError        **error);

/**
 * lrg_byte_reader_expect_u32:
 * @self: an #LrgByteReader
 * @expected: the value the data must hold (a magic number)
 * @error: return location for a #GError
 *
 * Reads a 32-bit value; anything but @expected is
 * %LRG_BYTE_ERROR_INVALID and leaves the cursor unchanged.
 *
 * Returns: %TRUE when the value matched
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_byte_reader_expect_u32 (LrgByteReader  *self,
                                     guint32         expected,
                                     GError        **error);

/**
 * lrg_byte_reader_read_cstring:
 * @self: an #LrgByteReader
 * @max_length: the longest string accepted (excluding the NUL), or 0 for
 *   no limit beyond the data
 * @error: return location for a #GError
 *
 * Reads a NUL-terminated string and moves past the NUL.  A string with
 * no NUL before the end (or before @max_length) is
 * %LRG_BYTE_ERROR_INVALID.  The bytes are returned as read; callers
 * validate UTF-8 when they need it.
 *
 * Returns: (transfer full) (nullable): the string
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_byte_reader_read_cstring (LrgByteReader  *self,
                                     gsize           max_length,
                                     GError        **error);

/**
 * lrg_byte_reader_read_fixed_string:
 * @self: an #LrgByteReader
 * @length: the field's width in bytes
 * @error: return location for a #GError
 *
 * Reads a fixed-width field and returns its text up to the first NUL
 * (or all @length bytes when there is none).
 *
 * Returns: (transfer full) (nullable): the string
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_byte_reader_read_fixed_string (LrgByteReader  *self,
                                          gsize           length,
                                          GError        **error);

/**
 * lrg_byte_reader_get_cstring_at:
 * @self: an #LrgByteReader
 * @offset: an offset from the start of the data
 * @error: return location for a #GError
 *
 * Reads the NUL-terminated string at @offset without moving the cursor
 * (string blocks: DBC string tables, M2 name offsets).
 *
 * Returns: (transfer full) (nullable): the string
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_byte_reader_get_cstring_at (LrgByteReader  *self,
                                       gsize           offset,
                                       GError        **error);

/**
 * lrg_byte_reader_new_sub_reader:
 * @self: an #LrgByteReader
 * @offset: the slice's start, from the start of the data
 * @length: the slice's length
 * @error: return location for a #GError
 *
 * A reader over a slice, sharing the data, with the same byte order.
 * Chunked formats parse each chunk with its own sub-reader so a chunk
 * can never read into its neighbour.
 *
 * Returns: (transfer full) (nullable): the sub-reader, or %NULL with
 *   %LRG_BYTE_ERROR_RANGE
 */
LRG_AVAILABLE_IN_ALL
LrgByteReader *lrg_byte_reader_new_sub_reader (LrgByteReader  *self,
                                               gsize           offset,
                                               gsize           length,
                                               GError        **error);

/**
 * lrg_byte_reader_read_sub_reader:
 * @self: an #LrgByteReader
 * @length: the slice's length, starting at the cursor
 * @error: return location for a #GError
 *
 * lrg_byte_reader_new_sub_reader() at the cursor, then moves the cursor
 * past the slice.
 *
 * Returns: (transfer full) (nullable): the sub-reader
 */
LRG_AVAILABLE_IN_ALL
LrgByteReader *lrg_byte_reader_read_sub_reader (LrgByteReader  *self,
                                                gsize           length,
                                                GError        **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgByteReader, lrg_byte_reader_unref)

G_END_DECLS

#endif /* LRG_BYTE_READER_H */
