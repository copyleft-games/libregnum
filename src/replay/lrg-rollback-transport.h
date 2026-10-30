/* lrg-rollback-transport.h - Unreliable datagram transport for rollback
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ROLLBACK_TRANSPORT (lrg_rollback_transport_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgRollbackTransport, lrg_rollback_transport, LRG, ROLLBACK_TRANSPORT, GObject)

/**
 * LrgRollbackTransportInterface:
 * @parent_iface: the parent interface
 * @send: sends one datagram to a peer, without blocking. Delivery, order
 *   and uniqueness are not guaranteed; the session's protocol tolerates
 *   loss, reordering and duplicates.
 * @receive: returns the next received datagram and its sender, or %NULL
 *   when none is waiting. Must not block.
 *
 * How an #LrgRollbackSession reaches its peers. Peers are addressed by
 * player index. Implementations: #LrgLoopbackTransport (in process, with
 * simulated latency, jitter and loss, for tests) and #LrgUdpTransport.
 *
 * Since: 0.4
 */
struct _LrgRollbackTransportInterface
{
    GTypeInterface parent_iface;

    /*< public >*/
    void     (*send)    (LrgRollbackTransport *self,
                         guint                 peer,
                         GBytes               *packet);
    GBytes * (*receive) (LrgRollbackTransport *self,
                         guint                *peer);

    /*< private >*/
    gpointer _reserved[4];
};

/**
 * lrg_rollback_transport_send:
 * @self: an #LrgRollbackTransport
 * @peer: the destination player index
 * @packet: the datagram
 *
 * Sends @packet to @peer (best effort).
 */
LRG_AVAILABLE_IN_ALL
void lrg_rollback_transport_send (LrgRollbackTransport *self,
                                  guint                 peer,
                                  GBytes               *packet);

/**
 * lrg_rollback_transport_receive:
 * @self: an #LrgRollbackTransport
 * @peer: (out): return location for the sender's player index
 *
 * Returns: (transfer full) (nullable): the next datagram, or %NULL
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_rollback_transport_receive (LrgRollbackTransport *self,
                                        guint                *peer);

G_END_DECLS
