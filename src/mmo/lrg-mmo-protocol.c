/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-protocol.h"
#include "lrg-mmo-service-private.h"
#include <zlib.h>
#define FRAME_LIMIT (1024u * 1024u)
#define HEADER_SIZE 24u

GBytes *
lrg_mmo_protocol_encode (guint opcode, guint64 sequence, GBytes *payload, gboolean compress, GError **error)
{
    const guint8 *data;
    gsize size;
    g_autofree guint8 *compressed = NULL;
    guint8 *wire;
    gsize stored;
    guint16 operation;
    guint32 length;
    guint64 number;
    gboolean used_compression = FALSE;
    g_return_val_if_fail (payload != NULL, NULL);
    data = g_bytes_get_data (payload, &size);
    if (opcode == 0 || opcode > 65535 || sequence == 0 || size > FRAME_LIMIT - HEADER_SIZE)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid protocol frame parameters");
        return NULL;
    }
    stored = size;
    if (compress && size > 0)
    {
        uLongf output_size = compressBound (size);
        compressed = g_malloc (output_size);
        if (compress2 (compressed, &output_size, data, size, Z_BEST_SPEED) != Z_OK)
        {
            _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "Compression failed");
            return NULL;
        }
        if (output_size < size)
        {
            data = compressed;
            stored = output_size;
            used_compression = TRUE;
        }
    }
    wire = g_malloc0 (HEADER_SIZE + stored);
    memcpy (wire, "LRGM", 4);
    wire[4] = 1;
    wire[5] = used_compression;
    operation = GUINT16_TO_BE ((guint16) opcode);
    memcpy (wire + 6, &operation, 2);
    number = GUINT64_TO_BE (sequence);
    memcpy (wire + 8, &number, 8);
    length = GUINT32_TO_BE ((guint32) size);
    memcpy (wire + 16, &length, 4);
    length = GUINT32_TO_BE ((guint32) stored);
    memcpy (wire + 20, &length, 4);
    if (stored > 0)
        memcpy (wire + HEADER_SIZE, data, stored);
    return g_bytes_new_take (wire, HEADER_SIZE + stored);
}

GBytes *
lrg_mmo_protocol_decode (GBytes *frame, guint *opcode, guint64 *sequence, GError **error)
{
    const guint8 *data;
    gsize size;
    guint16 operation;
    guint32 length, stored;
    guint64 number;
    g_autofree guint8 *output = NULL;
    g_return_val_if_fail (frame != NULL && opcode != NULL && sequence != NULL, NULL);
    *opcode = 0;
    *sequence = 0;
    data = g_bytes_get_data (frame, &size);
    if (size < HEADER_SIZE || size > FRAME_LIMIT || memcmp (data, "LRGM", 4) != 0 || data[4] != 1 || data[5] > 1)
        goto invalid;
    memcpy (&operation, data + 6, 2);
    memcpy (&number, data + 8, 8);
    memcpy (&length, data + 16, 4);
    memcpy (&stored, data + 20, 4);
    operation = GUINT16_FROM_BE (operation);
    number = GUINT64_FROM_BE (number);
    length = GUINT32_FROM_BE (length);
    stored = GUINT32_FROM_BE (stored);
    if (operation == 0 || number == 0 || length > FRAME_LIMIT - HEADER_SIZE || stored != size - HEADER_SIZE)
        goto invalid;
    if (data[5] == 0)
    {
        if (length != stored)
            goto invalid;
        *opcode = operation;
        *sequence = number;
        return g_bytes_new_from_bytes (frame, HEADER_SIZE, stored);
    }
    else
    {
        uLongf output_size = length;
        uLong input_size = stored;
        gint result;
        if (length == 0)
            goto invalid;
        output = g_malloc (length);
        result = uncompress2 (output, &output_size, data + HEADER_SIZE, &input_size);
        if (result != Z_OK || output_size != length || input_size != stored)
            goto invalid;
    }
    *opcode = operation;
    *sequence = number;
    return g_bytes_new_take (g_steal_pointer (&output), length);
invalid:
    _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid, unsupported or oversized protocol frame");
    return NULL;
}
