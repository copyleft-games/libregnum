/* lrg-udp-transport.h - UDP datagram transport for rollback sessions
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
#include "lrg-rollback-transport.h"

G_BEGIN_DECLS

/**
 * LRG_UDP_TRANSPORT_MAX_DATAGRAM:
 *
 * Largest datagram the transport sends or receives, in bytes (below the
 * common 1280-byte IPv6 minimum MTU after headers).
 *
 * Since: 0.4
 */
#define LRG_UDP_TRANSPORT_MAX_DATAGRAM (1200)

#define LRG_TYPE_UDP_TRANSPORT (lrg_udp_transport_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgUdpTransport, lrg_udp_transport, LRG, UDP_TRANSPORT, GObject)

/**
 * lrg_udp_transport_new:
 * @bind_address: (nullable): local address to bind, %NULL for any IPv4
 * @port: local port, 0 for an ephemeral one
 * @error: (nullable): return location for an error
 *
 * Opens a non-blocking UDP socket. Datagrams from addresses that are not
 * registered peers are dropped. There is no encryption or authentication:
 * use it on trusted networks, or wrap it (the MMO module's DTLS channel is
 * the authenticated alternative).
 *
 * Returns: (transfer full) (nullable): a new #LrgUdpTransport, or %NULL
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
LrgUdpTransport *lrg_udp_transport_new (const gchar  *bind_address,
                                        guint16       port,
                                        GError      **error);

/**
 * lrg_udp_transport_get_port:
 * @self: an #LrgUdpTransport
 *
 * Returns: the bound local port
 */
LRG_AVAILABLE_IN_ALL
guint16 lrg_udp_transport_get_port (LrgUdpTransport *self);

/**
 * lrg_udp_transport_add_peer:
 * @self: an #LrgUdpTransport
 * @peer: the peer's player index
 * @host: the peer's numeric IP address
 * @port: the peer's port
 * @error: (nullable): return location for an error
 *
 * Registers where player @peer is reached.
 *
 * Returns: %TRUE on success
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_udp_transport_add_peer (LrgUdpTransport  *self,
                                     guint             peer,
                                     const gchar      *host,
                                     guint16           port,
                                     GError          **error);

G_END_DECLS
