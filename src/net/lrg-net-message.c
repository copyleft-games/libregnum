/* lrg-net-message.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "net/lrg-net-message.h"

/*
 * Message wire format (big-endian):
 * - message_type: 1 byte
 * - flags: 1 byte (bit 0 = reliable)
 * - sender_id: 4 bytes
 * - receiver_id: 4 bytes
 * - sequence: 4 bytes
 * - timestamp: 8 bytes
 * - payload_length: 4 bytes
 * - payload: variable
 */
#define HEADER_SIZE 26

/**
 * LrgNetMessage:
 *
 * Internal structure for network messages.
 */
struct _LrgNetMessage
{
    LrgNetMessageType  message_type;
    guint32            sender_id;
    guint32            receiver_id;
    GBytes            *payload;
    gboolean           reliable;
    gint64             timestamp;
    guint32            sequence;
};

G_DEFINE_BOXED_TYPE (LrgNetMessage,
                     lrg_net_message,
                     lrg_net_message_copy,
                     lrg_net_message_free)

/**
 * lrg_net_message_new:
 * @message_type: the type of message
 * @sender_id: ID of the sending peer
 * @receiver_id: ID of the receiving peer (0 for broadcast)
 * @payload: (nullable): optional message payload
 *
 * Creates a new network message.
 *
 * Returns: (transfer full): A new #LrgNetMessage
 */
LrgNetMessage *
lrg_net_message_new (LrgNetMessageType  message_type,
                     guint32            sender_id,
                     guint32            receiver_id,
                     GBytes            *payload)
{
    LrgNetMessage *self;

    self = g_new0 (LrgNetMessage, 1);
    self->message_type = message_type;
    self->sender_id = sender_id;
    self->receiver_id = receiver_id;
    self->payload = payload ? g_bytes_ref (payload) : NULL;
    self->reliable = FALSE;
    self->timestamp = g_get_real_time ();
    self->sequence = 0;

    return self;
}

/**
 * lrg_net_message_copy:
 * @self: an #LrgNetMessage
 *
 * Creates a deep copy of the message.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgNetMessage *
lrg_net_message_copy (const LrgNetMessage *self)
{
    LrgNetMessage *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_new0 (LrgNetMessage, 1);
    copy->message_type = self->message_type;
    copy->sender_id = self->sender_id;
    copy->receiver_id = self->receiver_id;
    copy->payload = self->payload ? g_bytes_ref (self->payload) : NULL;
    copy->reliable = self->reliable;
    copy->timestamp = self->timestamp;
    copy->sequence = self->sequence;

    return copy;
}

/**
 * lrg_net_message_free:
 * @self: an #LrgNetMessage
 *
 * Frees the message and all associated data.
 */
void
lrg_net_message_free (LrgNetMessage *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->payload, g_bytes_unref);
    g_free (self);
}

/**
 * lrg_net_message_get_message_type:
 * @self: an #LrgNetMessage
 *
 * Gets the message type.
 *
 * Returns: The message type
 */
LrgNetMessageType
lrg_net_message_get_message_type (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, LRG_NET_MESSAGE_TYPE_DATA);
    return self->message_type;
}

/**
 * lrg_net_message_get_sender_id:
 * @self: an #LrgNetMessage
 *
 * Gets the sender peer ID.
 *
 * Returns: The sender ID
 */
guint32
lrg_net_message_get_sender_id (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->sender_id;
}

/**
 * lrg_net_message_get_receiver_id:
 * @self: an #LrgNetMessage
 *
 * Gets the receiver peer ID. A value of 0 indicates broadcast.
 *
 * Returns: The receiver ID
 */
guint32
lrg_net_message_get_receiver_id (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->receiver_id;
}

/**
 * lrg_net_message_get_payload:
 * @self: an #LrgNetMessage
 *
 * Gets the message payload.
 *
 * Returns: (transfer none) (nullable): The payload, or %NULL
 */
GBytes *
lrg_net_message_get_payload (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->payload;
}

/**
 * lrg_net_message_is_reliable:
 * @self: an #LrgNetMessage
 *
 * Checks if the message should be sent reliably.
 *
 * Returns: %TRUE if reliable delivery is required
 */
gboolean
lrg_net_message_is_reliable (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->reliable;
}

/**
 * lrg_net_message_set_reliable:
 * @self: an #LrgNetMessage
 * @reliable: whether to use reliable delivery
 *
 * Sets whether the message should be sent reliably.
 */
void
lrg_net_message_set_reliable (LrgNetMessage *self,
                              gboolean       reliable)
{
    g_return_if_fail (self != NULL);
    self->reliable = reliable;
}

/**
 * lrg_net_message_get_timestamp:
 * @self: an #LrgNetMessage
 *
 * Gets the message timestamp (microseconds since epoch).
 *
 * Returns: The timestamp
 */
gint64
lrg_net_message_get_timestamp (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->timestamp;
}

/**
 * lrg_net_message_get_sequence:
 * @self: an #LrgNetMessage
 *
 * Gets the message sequence number.
 *
 * Returns: The sequence number
 */
guint32
lrg_net_message_get_sequence (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->sequence;
}

/**
 * lrg_net_message_set_sequence:
 * @self: an #LrgNetMessage
 * @sequence: the sequence number
 *
 * Sets the message sequence number.
 */
void
lrg_net_message_set_sequence (LrgNetMessage *self,
                              guint32        sequence)
{
    g_return_if_fail (self != NULL);
    self->sequence = sequence;
}

/**
 * lrg_net_message_serialize:
 * @self: an #LrgNetMessage
 *
 * Serializes the message to bytes for network transmission.
 *
 * Returns: (transfer full): The serialized message
 */
GBytes *
lrg_net_message_serialize (const LrgNetMessage *self)
{
    guint8        *buffer;
    gsize          payload_size;
    gsize          total_size;
    gsize          offset;
    const guint8  *payload_data;
    guint8         flags;

    g_return_val_if_fail (self != NULL, NULL);

    payload_size = self->payload ? g_bytes_get_size (self->payload) : 0;
    total_size = HEADER_SIZE + payload_size;

    buffer = g_malloc (total_size);
    offset = 0;

    /* message_type: 1 byte */
    buffer[offset++] = (guint8) self->message_type;

    /* flags: 1 byte */
    flags = 0;
    if (self->reliable)
        flags |= 0x01;
    buffer[offset++] = flags;

    /* sender_id: 4 bytes (big-endian) */
    buffer[offset++] = (self->sender_id >> 24) & 0xFF;
    buffer[offset++] = (self->sender_id >> 16) & 0xFF;
    buffer[offset++] = (self->sender_id >> 8) & 0xFF;
    buffer[offset++] = self->sender_id & 0xFF;

    /* receiver_id: 4 bytes (big-endian) */
    buffer[offset++] = (self->receiver_id >> 24) & 0xFF;
    buffer[offset++] = (self->receiver_id >> 16) & 0xFF;
    buffer[offset++] = (self->receiver_id >> 8) & 0xFF;
    buffer[offset++] = self->receiver_id & 0xFF;

    /* sequence: 4 bytes (big-endian) */
    buffer[offset++] = (self->sequence >> 24) & 0xFF;
    buffer[offset++] = (self->sequence >> 16) & 0xFF;
    buffer[offset++] = (self->sequence >> 8) & 0xFF;
    buffer[offset++] = self->sequence & 0xFF;

    /* timestamp: 8 bytes (big-endian) */
    buffer[offset++] = (self->timestamp >> 56) & 0xFF;
    buffer[offset++] = (self->timestamp >> 48) & 0xFF;
    buffer[offset++] = (self->timestamp >> 40) & 0xFF;
    buffer[offset++] = (self->timestamp >> 32) & 0xFF;
    buffer[offset++] = (self->timestamp >> 24) & 0xFF;
    buffer[offset++] = (self->timestamp >> 16) & 0xFF;
    buffer[offset++] = (self->timestamp >> 8) & 0xFF;
    buffer[offset++] = self->timestamp & 0xFF;

    /* payload_length: 4 bytes (big-endian) */
    buffer[offset++] = (payload_size >> 24) & 0xFF;
    buffer[offset++] = (payload_size >> 16) & 0xFF;
    buffer[offset++] = (payload_size >> 8) & 0xFF;
    buffer[offset++] = payload_size & 0xFF;

    /* payload */
    if (payload_size > 0)
    {
        payload_data = g_bytes_get_data (self->payload, NULL);
        memcpy (buffer + offset, payload_data, payload_size);
    }

    return g_bytes_new_take (buffer, total_size);
}

/**
 * lrg_net_message_deserialize:
 * @data: serialized message data
 * @error: (nullable): return location for error
 *
 * Deserializes a message from bytes.
 *
 * Returns: (transfer full) (nullable): The deserialized message, or %NULL on error
 */
LrgNetMessage *
lrg_net_message_deserialize (GBytes   *data,
                             GError  **error)
{
    LrgNetMessage  *self;
    const guint8   *buffer;
    gsize           size;
    gsize           offset;
    guint8          flags;
    guint32         payload_size;
    GBytes         *payload;

    g_return_val_if_fail (data != NULL, NULL);

    buffer = g_bytes_get_data (data, &size);

    if (size < HEADER_SIZE)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_MESSAGE_INVALID,
                     "Message too short: got %zu bytes, need at least %d",
                     size, HEADER_SIZE);
        return NULL;
    }

    offset = 0;

    self = g_new0 (LrgNetMessage, 1);

    /* message_type: 1 byte */
    self->message_type = (LrgNetMessageType) buffer[offset++];

    /* flags: 1 byte */
    flags = buffer[offset++];
    self->reliable = (flags & 0x01) != 0;

    /* sender_id: 4 bytes (big-endian) */
    self->sender_id = ((guint32) buffer[offset] << 24) |
                      ((guint32) buffer[offset + 1] << 16) |
                      ((guint32) buffer[offset + 2] << 8) |
                      (guint32) buffer[offset + 3];
    offset += 4;

    /* receiver_id: 4 bytes (big-endian) */
    self->receiver_id = ((guint32) buffer[offset] << 24) |
                        ((guint32) buffer[offset + 1] << 16) |
                        ((guint32) buffer[offset + 2] << 8) |
                        (guint32) buffer[offset + 3];
    offset += 4;

    /* sequence: 4 bytes (big-endian) */
    self->sequence = ((guint32) buffer[offset] << 24) |
                     ((guint32) buffer[offset + 1] << 16) |
                     ((guint32) buffer[offset + 2] << 8) |
                     (guint32) buffer[offset + 3];
    offset += 4;

    /* timestamp: 8 bytes (big-endian) */
    self->timestamp = ((gint64) buffer[offset] << 56) |
                      ((gint64) buffer[offset + 1] << 48) |
                      ((gint64) buffer[offset + 2] << 40) |
                      ((gint64) buffer[offset + 3] << 32) |
                      ((gint64) buffer[offset + 4] << 24) |
                      ((gint64) buffer[offset + 5] << 16) |
                      ((gint64) buffer[offset + 6] << 8) |
                      (gint64) buffer[offset + 7];
    offset += 8;

    /* payload_length: 4 bytes (big-endian) */
    payload_size = ((guint32) buffer[offset] << 24) |
                   ((guint32) buffer[offset + 1] << 16) |
                   ((guint32) buffer[offset + 2] << 8) |
                   (guint32) buffer[offset + 3];
    offset += 4;

    /* Validate payload size */
    if (size < HEADER_SIZE + payload_size)
    {
        g_set_error (error,
                     LRG_NET_ERROR,
                     LRG_NET_ERROR_MESSAGE_INVALID,
                     "Message truncated: expected %u payload bytes, got %zu",
                     payload_size, size - HEADER_SIZE);
        lrg_net_message_free (self);
        return NULL;
    }

    /* payload */
    if (payload_size > 0)
    {
        payload = g_bytes_new_from_bytes (data, offset, payload_size);
        self->payload = payload;
    }
    else
    {
        self->payload = NULL;
    }

    return self;
}

/**
 * lrg_net_message_is_broadcast:
 * @self: an #LrgNetMessage
 *
 * Checks if this is a broadcast message (receiver_id == 0).
 *
 * Returns: %TRUE if this is a broadcast message
 */
gboolean
lrg_net_message_is_broadcast (const LrgNetMessage *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->receiver_id == 0;
}
