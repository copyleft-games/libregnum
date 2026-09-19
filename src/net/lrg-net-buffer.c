/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "config.h"
#include "lrg-net-buffer-private.h"

#define HEADER_SIZE 26u
#define PAYLOAD_LIMIT (1024u * 1024u)
#define QUEUE_LIMIT (4u * 1024u * 1024u)

void
_lrg_net_buffer_init (LrgNetBuffer *buffer)
{
    buffer->input = g_byte_array_new ();
    buffer->output = g_byte_array_new ();
}

void
_lrg_net_buffer_clear (LrgNetBuffer *buffer)
{
    g_clear_pointer (&buffer->input, g_byte_array_unref);
    g_clear_pointer (&buffer->output, g_byte_array_unref);
}

gboolean
_lrg_net_buffer_send (LrgNetBuffer  *buffer,
                      LrgNetMessage *message,
                      GError       **error)
{
    g_autoptr(GBytes) bytes = NULL;
    GBytes *payload;
    gsize size;
    gconstpointer data;

    payload = lrg_net_message_get_payload (message);
    if (payload != NULL && g_bytes_get_size (payload) > PAYLOAD_LIMIT)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE,
                             "Network payload exceeds 1 MiB");
        return FALSE;
    }
    bytes = lrg_net_message_serialize (message);
    data = g_bytes_get_data (bytes, &size);
    if (size > QUEUE_LIMIT - buffer->output->len)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK,
                             "Network output queue is full");
        return FALSE;
    }
    g_byte_array_append (buffer->output, data, size);
    return TRUE;
}

gboolean
_lrg_net_buffer_flush (LrgNetBuffer *buffer,
                       GSocket      *socket,
                       GError      **error)
{
    g_autoptr(GError) local_error = NULL;
    gssize count;

    if (buffer->output->len == 0)
        return TRUE;
    count = g_socket_send_with_blocking (socket, (gchar *) buffer->output->data,
                                        MIN (buffer->output->len, 65536u),
                                        FALSE, NULL, &local_error);
    if (count < 0)
    {
        if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            return TRUE;
        g_propagate_error (error, g_steal_pointer (&local_error));
        return FALSE;
    }
    if (count > 0)
        g_byte_array_remove_range (buffer->output, 0, count);
    return TRUE;
}

LrgNetMessage *
_lrg_net_buffer_receive (LrgNetBuffer *buffer,
                         GSocket      *socket,
                         GError      **error)
{
    guint attempts;

    /* Bound work per call and never read beyond the current frame. */
    for (attempts = 0; attempts < 16; attempts++)
    {
        g_autoptr(GError) local_error = NULL;
        g_autoptr(GBytes) bytes = NULL;
        LrgNetMessage *message;
        guint32 length;
        guint target = HEADER_SIZE;
        guint8 chunk[4096];
        gssize count;

        if (buffer->input->len >= HEADER_SIZE)
        {
            memcpy (&length, buffer->input->data + 22, sizeof length);
            length = GUINT32_FROM_BE (length);
            if (length > PAYLOAD_LIMIT)
            {
                g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE,
                                     "Network payload exceeds 1 MiB");
                return NULL;
            }
            target += length;
            if (buffer->input->len == target)
            {
                bytes = g_bytes_new (buffer->input->data, target);
                message = lrg_net_message_deserialize (bytes, error);
                g_byte_array_set_size (buffer->input, 0);
                return message;
            }
        }
        count = g_socket_receive_with_blocking (socket, (gchar *) chunk,
                                                MIN (sizeof chunk, target - buffer->input->len),
                                                FALSE, NULL, &local_error);
        if (count < 0)
        {
            if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
                g_propagate_error (error, g_steal_pointer (&local_error));
            return NULL;
        }
        if (count == 0)
        {
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                                 "Peer closed the connection");
            return NULL;
        }
        g_byte_array_append (buffer->input, chunk, count);
    }
    return NULL;
}
