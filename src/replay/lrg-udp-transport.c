/* lrg-udp-transport.c - UDP datagram transport for rollback sessions
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_REPLAY

#include "lrg-udp-transport.h"
#include "../lrg-log.h"

#include <gio/gio.h>

struct _LrgUdpTransport
{
    GObject     parent_instance;

    GSocket    *socket;
    guint16     port;
    GHashTable *peers;   /* GUINT_TO_POINTER (peer) -> GInetSocketAddress */
};

static void lrg_udp_transport_iface_init (LrgRollbackTransportInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (LrgUdpTransport, lrg_udp_transport, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_ROLLBACK_TRANSPORT,
                                                      lrg_udp_transport_iface_init))

static void
lrg_udp_transport_finalize (GObject *object)
{
    LrgUdpTransport *self = LRG_UDP_TRANSPORT (object);

    if (self->socket != NULL)
        g_socket_close (self->socket, NULL);
    g_clear_object (&self->socket);
    g_clear_pointer (&self->peers, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_udp_transport_parent_class)->finalize (object);
}

static void
lrg_udp_transport_class_init (LrgUdpTransportClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_udp_transport_finalize;
}

static void
lrg_udp_transport_init (LrgUdpTransport *self)
{
    self->peers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
}

LrgUdpTransport *
lrg_udp_transport_new (const gchar  *bind_address,
                       guint16       port,
                       GError      **error)
{
    g_autoptr(LrgUdpTransport) self = NULL;
    g_autoptr(GInetAddress) inet = NULL;
    g_autoptr(GSocketAddress) address = NULL;
    g_autoptr(GSocketAddress) local = NULL;

    /* 1. Resolve the bind address (numeric only: no blocking DNS) */
    if (bind_address != NULL)
        inet = g_inet_address_new_from_string (bind_address);
    else
        inet = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
    if (inet == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "'%s' is not a numeric IP address", bind_address);
        return NULL;
    }

    /* 2. A non-blocking datagram socket */
    self = g_object_new (LRG_TYPE_UDP_TRANSPORT, NULL);
    self->socket = g_socket_new (g_inet_address_get_family (inet), G_SOCKET_TYPE_DATAGRAM,
                                 G_SOCKET_PROTOCOL_UDP, error);
    if (self->socket == NULL)
        return NULL;
    g_socket_set_blocking (self->socket, FALSE);

    address = g_inet_socket_address_new (inet, port);
    if (!g_socket_bind (self->socket, address, FALSE, error))
        return NULL;

    /* 3. Learn the port the kernel picked for port 0 */
    local = g_socket_get_local_address (self->socket, error);
    if (local == NULL)
        return NULL;
    self->port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (local));

    return g_steal_pointer (&self);
}

guint16
lrg_udp_transport_get_port (LrgUdpTransport *self)
{
    g_return_val_if_fail (LRG_IS_UDP_TRANSPORT (self), 0);

    return self->port;
}

gboolean
lrg_udp_transport_add_peer (LrgUdpTransport  *self,
                            guint             peer,
                            const gchar      *host,
                            guint16           port,
                            GError          **error)
{
    g_autoptr(GInetAddress) inet = NULL;

    g_return_val_if_fail (LRG_IS_UDP_TRANSPORT (self), FALSE);
    g_return_val_if_fail (host != NULL, FALSE);

    inet = g_inet_address_new_from_string (host);
    if (inet == NULL)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "'%s' is not a numeric IP address", host);
        return FALSE;
    }
    g_hash_table_insert (self->peers, GUINT_TO_POINTER (peer),
                         g_inet_socket_address_new (inet, port));
    return TRUE;
}

static void
lrg_udp_transport_send (LrgRollbackTransport *transport,
                        guint                 peer,
                        GBytes               *packet)
{
    LrgUdpTransport *self = LRG_UDP_TRANSPORT (transport);
    g_autoptr(GError) error = NULL;
    GSocketAddress *address;
    gconstpointer data;
    gsize size;

    address = g_hash_table_lookup (self->peers, GUINT_TO_POINTER (peer));
    data = g_bytes_get_data (packet, &size);
    if (address == NULL || size > LRG_UDP_TRANSPORT_MAX_DATAGRAM)
    {
        lrg_log_debug ("Dropping datagram for peer %u (%s)", peer,
                       address == NULL ? "unknown peer" : "too large");
        return;
    }

    /* Best effort: a full send buffer is just another lost datagram */
    if (g_socket_send_to (self->socket, address, data, size, NULL, &error) < 0)
        lrg_log_debug ("UDP send to peer %u failed: %s", peer, error->message);
}

static gboolean
peer_matches (GInetSocketAddress *a,
              GInetSocketAddress *b)
{
    return g_inet_socket_address_get_port (a) == g_inet_socket_address_get_port (b) &&
           g_inet_address_equal (g_inet_socket_address_get_address (a),
                                 g_inet_socket_address_get_address (b));
}

static GBytes *
lrg_udp_transport_receive (LrgRollbackTransport *transport,
                           guint                *peer)
{
    LrgUdpTransport *self = LRG_UDP_TRANSPORT (transport);
    guint8 buffer[LRG_UDP_TRANSPORT_MAX_DATAGRAM];

    /* Read until a datagram from a known peer arrives or the socket is empty */
    for (;;)
    {
        g_autoptr(GSocketAddress) source = NULL;
        g_autoptr(GError) error = NULL;
        GHashTableIter iter;
        gpointer key;
        gpointer value;
        gssize received;

        received = g_socket_receive_from (self->socket, &source, (gchar *) buffer,
                                          sizeof (buffer), NULL, &error);
        if (received < 0)
        {
            if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                lrg_log_debug ("UDP receive failed: %s", error->message);
            return NULL;
        }
        if (source == NULL || !G_IS_INET_SOCKET_ADDRESS (source))
            continue;

        g_hash_table_iter_init (&iter, self->peers);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            if (peer_matches (G_INET_SOCKET_ADDRESS (source), G_INET_SOCKET_ADDRESS (value)))
            {
                *peer = GPOINTER_TO_UINT (key);
                return g_bytes_new (buffer, (gsize) received);
            }
        }
        /* Not a peer: drop and keep reading */
    }
}

static void
lrg_udp_transport_iface_init (LrgRollbackTransportInterface *iface)
{
    iface->send = lrg_udp_transport_send;
    iface->receive = lrg_udp_transport_receive;
}
