/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_DATAGRAM (lrg_mmo_datagram_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoDatagram, lrg_mmo_datagram, LRG, MMO_DATAGRAM, GObject)
/**
 * lrg_mmo_datagram_new:
 * @socket: connected UDP socket dedicated to this peer
 * @server: whether to accept as server
 * @identity: (nullable): expected DNS server identity for clients
 * @certificate: PEM certificate file
 * @private_key: PEM private key file
 * @authorities: PEM trust anchors file
 * @error: (nullable): error return
 *
 * Creates a mutually authenticated DTLS channel. Complete handshake() before sending. Bind the verified peer certificate
 * to an account through trusted ingress policy; a trusted CA alone is not an
 * account authorization decision. Each instance is thread-confined.
 * Returns: (transfer full) (nullable): channel
 */
LRG_AVAILABLE_IN_ALL
LrgMmoDatagram *lrg_mmo_datagram_new (GSocket *socket, gboolean server,
                                     const gchar *identity, const gchar *certificate,
                                     const gchar *private_key, const gchar *authorities, GError **error);
/**
 * lrg_mmo_datagram_handshake:
 * @self: channel
 * @error: (nullable): error return
 *
 * Advances a nonblocking handshake, including DTLS retransmission timers. Call
 * periodically until TRUE; WOULD_BLOCK means pending. Enforce a host deadline
 * (recommended ten seconds) and discard the channel on expiry or cancellation.
 * Other errors make the channel unusable. No unauthenticated application data
 * is accepted. Provision account-specific certificates or bind this channel to
 * an already authenticated session before admitting game commands.
 * Returns: whether handshake completed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_datagram_handshake (LrgMmoDatagram *self, GError **error);
/**
 * lrg_mmo_datagram_send:
 * @self: channel
 * @sequence: nonzero application packet sequence, never reuse for new data
 * @payload: up to 1100 bytes
 * @error: (nullable): error return
 *
 * Nonblocking, no queue or implicit retransmission. After WOULD_BLOCK retry
 * the identical sequence and payload until sent; another payload returns PENDING.
 * A successful send does not
 * guarantee delivery. Use higher-layer acknowledgments and periodic snapshots.
 * Returns: whether sent
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_datagram_send (LrgMmoDatagram *self, guint64 sequence, GBytes *payload, GError **error);
/**
 * lrg_mmo_datagram_receive:
 * @self: channel
 * @sequence: (out): received packet sequence
 * @error: (nullable): error return
 *
 * Nonblocking, consumes at most one packet. Accepts reordered packets within a
 * 64-sequence window. Duplicates, old packets and invalid frames report
 * INVALID_DATA; no data reports WOULD_BLOCK. A new connection resets the window.
 * Returns: (transfer full) (nullable): payload
 */
LRG_AVAILABLE_IN_ALL
GBytes *lrg_mmo_datagram_receive (LrgMmoDatagram *self, guint64 *sequence, GError **error);
G_END_DECLS
