/* lrg-rollback-transport.c - Unreliable datagram transport for rollback
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-rollback-transport.h"

G_DEFINE_INTERFACE (LrgRollbackTransport, lrg_rollback_transport, G_TYPE_OBJECT)

static void
lrg_rollback_transport_default_init (LrgRollbackTransportInterface *iface)
{
    /* send and receive are required */
}

void
lrg_rollback_transport_send (LrgRollbackTransport *self,
                             guint                 peer,
                             GBytes               *packet)
{
    LrgRollbackTransportInterface *iface;

    g_return_if_fail (LRG_IS_ROLLBACK_TRANSPORT (self));
    g_return_if_fail (packet != NULL);

    iface = LRG_ROLLBACK_TRANSPORT_GET_IFACE (self);
    g_return_if_fail (iface->send != NULL);

    iface->send (self, peer, packet);
}

GBytes *
lrg_rollback_transport_receive (LrgRollbackTransport *self,
                                guint                *peer)
{
    LrgRollbackTransportInterface *iface;
    guint unused;

    g_return_val_if_fail (LRG_IS_ROLLBACK_TRANSPORT (self), NULL);

    iface = LRG_ROLLBACK_TRANSPORT_GET_IFACE (self);
    g_return_val_if_fail (iface->receive != NULL, NULL);

    return iface->receive (self, peer != NULL ? peer : &unused);
}
