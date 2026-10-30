/* lrg-state-buffer.h - Binary buffer for simulation state and checksums
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 *
 * LrgStateBuffer is the serialization surface of the determinism layer:
 * #LrgSimStateful objects write their simulation state into it (little-endian,
 * floats as raw IEEE-754 bit patterns, so a round trip is exact), the rollback
 * session keeps one per saved tick, and the desync detector hashes it.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_STATE_BUFFER (lrg_state_buffer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgStateBuffer, lrg_state_buffer, LRG, STATE_BUFFER, GObject)

/**
 * lrg_state_buffer_new:
 *
 * Creates an empty buffer in write mode: every write appends at the end.
 *
 * Returns: (transfer full): a new #LrgStateBuffer
 */
LRG_AVAILABLE_IN_ALL
LrgStateBuffer *lrg_state_buffer_new (void);

/**
 * lrg_state_buffer_new_from_bytes:
 * @bytes: serialized state
 *
 * Creates a buffer holding a copy of @bytes with its read cursor at the start.
 *
 * Returns: (transfer full): a new #LrgStateBuffer
 */
LRG_AVAILABLE_IN_ALL
LrgStateBuffer *lrg_state_buffer_new_from_bytes (GBytes *bytes);

/**
 * lrg_state_buffer_new_from_data:
 * @data: (array length=size) (element-type guint8): serialized state
 * @size: length of @data in bytes
 *
 * Creates a buffer holding a copy of @data with its read cursor at the start.
 *
 * Returns: (transfer full): a new #LrgStateBuffer
 */
LRG_AVAILABLE_IN_ALL
LrgStateBuffer *lrg_state_buffer_new_from_data (const guint8 *data,
                                                gsize         size);

/**
 * lrg_state_buffer_reset:
 * @self: an #LrgStateBuffer
 *
 * Empties the buffer, rewinds the read cursor and clears the overrun flag.
 * The allocation is kept, so a buffer reused every tick does not reallocate.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_reset (LrgStateBuffer *self);

/**
 * lrg_state_buffer_rewind:
 * @self: an #LrgStateBuffer
 *
 * Moves the read cursor back to the start and clears the overrun flag.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_rewind (LrgStateBuffer *self);

/**
 * lrg_state_buffer_get_size:
 * @self: an #LrgStateBuffer
 *
 * Returns: the number of bytes written
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_state_buffer_get_size (LrgStateBuffer *self);

/**
 * lrg_state_buffer_get_position:
 * @self: an #LrgStateBuffer
 *
 * Returns: the read cursor, in bytes from the start
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_state_buffer_get_position (LrgStateBuffer *self);

/**
 * lrg_state_buffer_get_remaining:
 * @self: an #LrgStateBuffer
 *
 * Returns: the number of bytes left to read
 */
LRG_AVAILABLE_IN_ALL
gsize lrg_state_buffer_get_remaining (LrgStateBuffer *self);

/**
 * lrg_state_buffer_get_data:
 * @self: an #LrgStateBuffer
 * @size: (out) (optional): return location for the size in bytes
 *
 * Returns: (transfer none) (array length=size) (element-type guint8):
 *   the written bytes, valid until the next write or reset
 */
LRG_AVAILABLE_IN_ALL
const guint8 *lrg_state_buffer_get_data (LrgStateBuffer *self,
                                         gsize          *size);

/**
 * lrg_state_buffer_to_bytes:
 * @self: an #LrgStateBuffer
 *
 * Returns: (transfer full): a copy of the written bytes
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_state_buffer_to_bytes (LrgStateBuffer *self);

/**
 * lrg_state_buffer_checksum:
 * @self: an #LrgStateBuffer
 *
 * Hashes the written bytes with 64-bit FNV-1a. Equal state serializations
 * give equal checksums on every platform.
 *
 * Returns: the checksum
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_state_buffer_checksum (LrgStateBuffer *self);

/**
 * lrg_state_hash_data:
 * @data: (array length=size) (element-type guint8) (nullable): bytes to hash
 * @size: length of @data
 * @seed: previous hash to continue from, or 0 to start a new one
 *
 * 64-bit FNV-1a over @data, continuing from @seed (0 selects the standard
 * offset basis), so a hash can be built over several blocks.
 *
 * Returns: the hash
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_state_hash_data (const guint8 *data,
                             gsize         size,
                             guint64       seed);

/**
 * lrg_state_buffer_write_uint8:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends one byte.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_uint8 (LrgStateBuffer *self,
                                   guint8          value);

/**
 * lrg_state_buffer_write_boolean:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a boolean as one byte (0 or 1).
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_boolean (LrgStateBuffer *self,
                                     gboolean        value);

/**
 * lrg_state_buffer_write_uint32:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a 32-bit unsigned integer, little-endian.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_uint32 (LrgStateBuffer *self,
                                    guint32         value);

/**
 * lrg_state_buffer_write_int32:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a 32-bit signed integer, little-endian two's complement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_int32 (LrgStateBuffer *self,
                                   gint32          value);

/**
 * lrg_state_buffer_write_uint64:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a 64-bit unsigned integer, little-endian.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_uint64 (LrgStateBuffer *self,
                                    guint64         value);

/**
 * lrg_state_buffer_write_int64:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a 64-bit signed integer, little-endian two's complement.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_int64 (LrgStateBuffer *self,
                                   gint64          value);

/**
 * lrg_state_buffer_write_float:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a float as its raw 32-bit IEEE-754 pattern (exact round trip,
 * NaN payloads and signed zeros included).
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_float (LrgStateBuffer *self,
                                   gfloat          value);

/**
 * lrg_state_buffer_write_double:
 * @self: an #LrgStateBuffer
 * @value: the value
 *
 * Appends a double as its raw 64-bit IEEE-754 pattern.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_double (LrgStateBuffer *self,
                                    gdouble         value);

/**
 * lrg_state_buffer_write_string:
 * @self: an #LrgStateBuffer
 * @value: (nullable): a UTF-8 string
 *
 * Appends a length-prefixed string (a %NULL string is stored as a distinct
 * marker and reads back as %NULL).
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_string (LrgStateBuffer *self,
                                    const gchar    *value);

/**
 * lrg_state_buffer_write_data:
 * @self: an #LrgStateBuffer
 * @data: (array length=size) (element-type guint8) (nullable): bytes
 * @size: length of @data
 *
 * Appends a length-prefixed byte block.
 */
LRG_AVAILABLE_IN_ALL
void lrg_state_buffer_write_data (LrgStateBuffer *self,
                                  const guint8   *data,
                                  gsize           size);

/**
 * lrg_state_buffer_read_uint8:
 * @self: an #LrgStateBuffer
 *
 * Reads one byte. Reading past the end returns 0 and sets the overrun flag
 * (see lrg_state_buffer_check()).
 *
 * Returns: the value
 */
LRG_AVAILABLE_IN_ALL
guint8 lrg_state_buffer_read_uint8 (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_boolean:
 * @self: an #LrgStateBuffer
 *
 * Returns: the boolean written by lrg_state_buffer_write_boolean()
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_state_buffer_read_boolean (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_uint32:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_state_buffer_read_uint32 (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_int32:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
gint32 lrg_state_buffer_read_int32 (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_uint64:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_state_buffer_read_uint64 (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_int64:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_state_buffer_read_int64 (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_float:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_state_buffer_read_float (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_double:
 * @self: an #LrgStateBuffer
 *
 * Returns: the value, or 0 on overrun
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_state_buffer_read_double (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_string:
 * @self: an #LrgStateBuffer
 *
 * Returns: (transfer full) (nullable): the string, or %NULL if a %NULL
 *   string was written or on overrun
 */
LRG_AVAILABLE_IN_ALL
gchar *lrg_state_buffer_read_string (LrgStateBuffer *self);

/**
 * lrg_state_buffer_read_data:
 * @self: an #LrgStateBuffer
 *
 * Returns: (transfer full) (nullable): the byte block, or %NULL on overrun
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_state_buffer_read_data (LrgStateBuffer *self);

/**
 * lrg_state_buffer_check:
 * @self: an #LrgStateBuffer
 * @error: (nullable): return location for an error
 *
 * Reports whether any read ran past the end since the last rewind or reset.
 *
 * Returns: %TRUE if every read was in bounds, %FALSE (with
 *   %G_IO_ERROR_INVALID_DATA) otherwise
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_state_buffer_check (LrgStateBuffer  *self,
                                 GError         **error);

G_END_DECLS
