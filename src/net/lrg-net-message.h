/* lrg-net-message.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Network message structure for peer-to-peer communication.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib.h>
#include <glib-object.h>
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_NET_MESSAGE (lrg_net_message_get_type ())

/**
 * LrgNetMessage:
 *
 * A network message for peer-to-peer communication.
 *
 * Network messages are the fundamental unit of communication in
 * the networking system. Each message has a type, sender/receiver
 * IDs, and an optional payload. Messages can be marked as reliable
 * to ensure delivery.
 */
typedef struct _LrgNetMessage LrgNetMessage;

#pragma GCC visibility push(default)

GType           lrg_net_message_get_type          (void) G_GNUC_CONST;

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
LrgNetMessage  *lrg_net_message_new               (LrgNetMessageType  message_type,
                                                   guint32            sender_id,
                                                   guint32            receiver_id,
                                                   GBytes            *payload);

/**
 * lrg_net_message_copy:
 * @self: an #LrgNetMessage
 *
 * Creates a deep copy of the message.
 *
 * Returns: (transfer full): A copy of @self
 */
LrgNetMessage  *lrg_net_message_copy              (const LrgNetMessage *self);

/**
 * lrg_net_message_free:
 * @self: an #LrgNetMessage
 *
 * Frees the message and all associated data.
 */
void            lrg_net_message_free              (LrgNetMessage *self);

/**
 * lrg_net_message_get_message_type:
 * @self: an #LrgNetMessage
 *
 * Gets the message type.
 *
 * Returns: The message type
 */
LrgNetMessageType lrg_net_message_get_message_type (const LrgNetMessage *self);

/**
 * lrg_net_message_get_sender_id:
 * @self: an #LrgNetMessage
 *
 * Gets the sender peer ID.
 *
 * Returns: The sender ID
 */
guint32         lrg_net_message_get_sender_id     (const LrgNetMessage *self);

/**
 * lrg_net_message_get_receiver_id:
 * @self: an #LrgNetMessage
 *
 * Gets the receiver peer ID. A value of 0 indicates broadcast.
 *
 * Returns: The receiver ID
 */
guint32         lrg_net_message_get_receiver_id   (const LrgNetMessage *self);

/**
 * lrg_net_message_get_payload:
 * @self: an #LrgNetMessage
 *
 * Gets the message payload.
 *
 * Returns: (transfer none) (nullable): The payload, or %NULL
 */
GBytes         *lrg_net_message_get_payload       (const LrgNetMessage *self);

/**
 * lrg_net_message_is_reliable:
 * @self: an #LrgNetMessage
 *
 * Checks if the message should be sent reliably.
 *
 * Returns: %TRUE if reliable delivery is required
 */
gboolean        lrg_net_message_is_reliable       (const LrgNetMessage *self);

/**
 * lrg_net_message_set_reliable:
 * @self: an #LrgNetMessage
 * @reliable: whether to use reliable delivery
 *
 * Sets whether the message should be sent reliably.
 */
void            lrg_net_message_set_reliable      (LrgNetMessage *self,
                                                   gboolean       reliable);

/**
 * lrg_net_message_get_timestamp:
 * @self: an #LrgNetMessage
 *
 * Gets the message timestamp (microseconds since epoch).
 *
 * Returns: The timestamp
 */
gint64          lrg_net_message_get_timestamp     (const LrgNetMessage *self);

/**
 * lrg_net_message_get_sequence:
 * @self: an #LrgNetMessage
 *
 * Gets the message sequence number.
 *
 * Returns: The sequence number
 */
guint32         lrg_net_message_get_sequence      (const LrgNetMessage *self);

/**
 * lrg_net_message_set_sequence:
 * @self: an #LrgNetMessage
 * @sequence: the sequence number
 *
 * Sets the message sequence number.
 */
void            lrg_net_message_set_sequence      (LrgNetMessage *self,
                                                   guint32        sequence);

/**
 * lrg_net_message_serialize:
 * @self: an #LrgNetMessage
 *
 * Serializes the message to bytes for network transmission.
 *
 * Returns: (transfer full): The serialized message
 */
GBytes         *lrg_net_message_serialize         (const LrgNetMessage *self);

/**
 * lrg_net_message_deserialize:
 * @data: serialized message data
 * @error: (nullable): return location for error
 *
 * Deserializes a message from bytes.
 *
 * Returns: (transfer full) (nullable): The deserialized message, or %NULL on error
 */
LrgNetMessage  *lrg_net_message_deserialize       (GBytes   *data,
                                                   GError  **error);

/**
 * lrg_net_message_is_broadcast:
 * @self: an #LrgNetMessage
 *
 * Checks if this is a broadcast message (receiver_id == 0).
 *
 * Returns: %TRUE if this is a broadcast message
 */
gboolean        lrg_net_message_is_broadcast      (const LrgNetMessage *self);

#pragma GCC visibility pop

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgNetMessage, lrg_net_message_free)

G_END_DECLS
