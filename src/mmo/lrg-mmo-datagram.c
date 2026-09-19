/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-datagram.h"
#include "lrg-mmo-service-private.h"
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>

struct _LrgMmoDatagram
{
    GObject parent_instance;
    GSocket *socket;
    SSL_CTX *context;
    SSL *ssl;
    gboolean ready;
    gboolean failed;
    GBytes *pending;
    guint64 pending_sequence;
    guint64 highest;
    guint64 seen;
};
G_DEFINE_TYPE (LrgMmoDatagram, lrg_mmo_datagram, G_TYPE_OBJECT)

static void
lrg_mmo_datagram_finalize (GObject *object)
{
    LrgMmoDatagram *self = LRG_MMO_DATAGRAM (object);
    g_clear_pointer (&self->pending, g_bytes_unref);
    SSL_free (self->ssl);
    SSL_CTX_free (self->context);
    g_clear_object (&self->socket);
    G_OBJECT_CLASS (lrg_mmo_datagram_parent_class)->finalize (object);
}

static void
lrg_mmo_datagram_class_init (LrgMmoDatagramClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_datagram_finalize;
}

static void
lrg_mmo_datagram_init (LrgMmoDatagram *self)
{
}

LrgMmoDatagram *
lrg_mmo_datagram_new (GSocket *socket, gboolean server, const gchar *identity,
                       const gchar *certificate, const gchar *private_key,
                       const gchar *authorities, GError **error)
{
    g_autoptr(LrgMmoDatagram) self = NULL;
    struct sockaddr_storage peer;
    socklen_t peer_size = sizeof peer;
    BIO *bio;
    if (!G_IS_SOCKET (socket) || g_socket_get_socket_type (socket) != G_SOCKET_TYPE_DATAGRAM ||
        !g_socket_is_connected (socket) || certificate == NULL || private_key == NULL ||
        authorities == NULL || (!server && (identity == NULL || *identity == '\0')))
    {
        _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Connected UDP socket, identity and credentials required");
        return NULL;
    }
    self = g_object_new (LRG_TYPE_MMO_DATAGRAM, NULL);
    self->socket = g_object_ref (socket);
    g_socket_set_blocking (socket, FALSE);
    self->context = SSL_CTX_new (DTLS_method ());
    if (self->context == NULL ||
        !SSL_CTX_set_min_proto_version (self->context, DTLS1_2_VERSION) ||
        SSL_CTX_use_certificate_chain_file (self->context, certificate) != 1 ||
        SSL_CTX_use_PrivateKey_file (self->context, private_key, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key (self->context) != 1 ||
        SSL_CTX_load_verify_locations (self->context, authorities, NULL) != 1)
        goto failed;
    SSL_CTX_set_verify (self->context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    self->ssl = SSL_new (self->context);
    if (self->ssl == NULL || getpeername (g_socket_get_fd (socket), (struct sockaddr *) &peer, &peer_size) != 0)
        goto failed;
    bio = BIO_new_dgram (g_socket_get_fd (socket), BIO_NOCLOSE);
    if (bio == NULL)
        goto failed;
    BIO_ctrl (bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &peer);
    SSL_set_bio (self->ssl, bio, bio);
    SSL_set_options (self->ssl, SSL_OP_NO_QUERY_MTU);
    SSL_set_mode (self->ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_set_mtu (self->ssl, 1200);
    if (server)
        SSL_set_accept_state (self->ssl);
    else
    {
        if (SSL_set1_host (self->ssl, identity) != 1 || SSL_set_tlsext_host_name (self->ssl, identity) != 1)
            goto failed;
        SSL_set_connect_state (self->ssl);
    }
    return g_steal_pointer (&self);
failed:
    ERR_clear_error ();
    _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "Could not configure DTLS credentials or socket");
    return NULL;
}

static gboolean
ssl_result (LrgMmoDatagram *self, gint result, GError **error)
{
    gint code = SSL_get_error (self->ssl, result);
    if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
        return _lrg_mmo_fail (error, G_IO_ERROR_WOULD_BLOCK, "DTLS operation pending");
    self->failed = TRUE;
    ERR_clear_error ();
    return _lrg_mmo_fail (error, G_IO_ERROR_FAILED, "DTLS peer verification or transport failed");
}

gboolean
lrg_mmo_datagram_handshake (LrgMmoDatagram *self, GError **error)
{
    gint result;
    g_return_val_if_fail (LRG_IS_MMO_DATAGRAM (self), FALSE);
    if (self->failed)
        return _lrg_mmo_fail (error, G_IO_ERROR_CLOSED, "DTLS channel failed");
    if (self->ready)
        return TRUE;
    ERR_clear_error ();
    if (DTLSv1_handle_timeout (self->ssl) < 0)
    {
        self->failed = TRUE;
        return _lrg_mmo_fail (error, G_IO_ERROR_TIMED_OUT, "DTLS handshake timed out");
    }
    result = SSL_do_handshake (self->ssl);
    if (result != 1)
        return ssl_result (self, result, error);
    self->ready = TRUE;
    return TRUE;
}

gboolean
lrg_mmo_datagram_send (LrgMmoDatagram *self, guint64 sequence, GBytes *payload, GError **error)
{
    guint8 buffer[1112];
    guint64 wire_sequence;
    gint result;
    gsize size;
    g_return_val_if_fail (LRG_IS_MMO_DATAGRAM (self), FALSE);
    if (payload == NULL || sequence == 0 || g_bytes_get_size (payload) > 1100)
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Invalid datagram sequence or size");
    size = g_bytes_get_size (payload);
    memcpy (buffer, "LRGD", 4);
    wire_sequence = GUINT64_TO_BE (sequence);
    memcpy (buffer + 4, &wire_sequence, 8);
    if (size != 0)
        memcpy (buffer + 12, g_bytes_get_data (payload, NULL), size);
    if (!self->ready || self->failed)
        return _lrg_mmo_fail (error, G_IO_ERROR_NOT_CONNECTED, "DTLS handshake required");
    if (self->pending != NULL && (sequence != self->pending_sequence || !g_bytes_equal (payload, self->pending)))
        return _lrg_mmo_fail (error, G_IO_ERROR_PENDING, "Retry the pending datagram unchanged");
    if (self->pending == NULL)
    {
        self->pending = g_bytes_ref (payload);
        self->pending_sequence = sequence;
    }
    ERR_clear_error ();
    result = SSL_write (self->ssl, buffer, size + 12);
    if (result <= 0)
        return ssl_result (self, result, error);
    g_clear_pointer (&self->pending, g_bytes_unref);
    return TRUE;
}

GBytes *
lrg_mmo_datagram_receive (LrgMmoDatagram *self, guint64 *sequence, GError **error)
{
    guint8 buffer[1113];
    guint64 number, distance;
    gint count;
    g_return_val_if_fail (LRG_IS_MMO_DATAGRAM (self), NULL);
    g_return_val_if_fail (sequence != NULL, NULL);
    *sequence = 0;
    if (!self->ready || self->failed)
    {
        _lrg_mmo_fail (error, G_IO_ERROR_NOT_CONNECTED, "DTLS handshake required");
        return NULL;
    }
    ERR_clear_error ();
    count = SSL_read (self->ssl, buffer, sizeof buffer);
    if (count <= 0)
    {
        ssl_result (self, count, error);
        return NULL;
    }
    if (count < 12 || count > 1112 ||
        memcmp (buffer, "LRGD", 4) != 0)
        goto invalid;
    memcpy (&number, buffer + 4, 8);
    number = GUINT64_FROM_BE (number);
    if (number == 0)
        goto invalid;
    if (number > self->highest)
    {
        distance = number - self->highest;
        self->seen = distance >= 64 ? 1 : (self->seen << distance) | 1;
        self->highest = number;
    }
    else
    {
        distance = self->highest - number;
        if (distance >= 64 || (self->seen & (((guint64) 1) << distance)))
            goto invalid;
        self->seen |= ((guint64) 1) << distance;
    }
    *sequence = number;
    return g_bytes_new (buffer + 12, count - 12);
invalid:
    _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Invalid, duplicate or expired datagram");
    return NULL;
}
