/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
/**
 * lrg_mmo_protocol_encode:
 * @opcode: application operation code, 1 to 65535
 * @sequence: nonzero message sequence
 * @payload: payload, at most 1048552 bytes
 * @compress: whether to try zlib compression; never compress secrets mixed with untrusted data
 * @error: (nullable): error return
 *
 * Encodes protocol version one with a fixed big-endian header and optional bounded
 * compression. The complete frame fits the net transport's 1 MiB payload limit.
 * Opcode schemas and stream identity are negotiated by the host.
 *
 * Returns: (transfer full) (nullable): wire frame, or NULL
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_mmo_protocol_encode (guint opcode, guint64 sequence, GBytes *payload,
                                  gboolean compress, GError **error);
/**
 * lrg_mmo_protocol_decode:
 * @frame: untrusted wire frame
 * @opcode: (out): decoded operation code
 * @sequence: (out): decoded message sequence
 * @error: (nullable): error return
 *
 * Rejects unknown versions/flags, invalid lengths, trailing data and decompression
 * bombs. Decoding does not authenticate the sender or authorize its operation.
 *
 * Returns: (transfer full) (nullable): bounded payload, or NULL
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_mmo_protocol_decode (GBytes *frame, guint *opcode, guint64 *sequence, GError **error);
G_END_DECLS
