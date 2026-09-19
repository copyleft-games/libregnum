/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-net-message.h"
#include <gio/gio.h>

typedef struct
{
    GByteArray *input;
    GByteArray *output;
} LrgNetBuffer;

G_GNUC_INTERNAL void _lrg_net_buffer_init (LrgNetBuffer *buffer);
G_GNUC_INTERNAL void _lrg_net_buffer_clear (LrgNetBuffer *buffer);
G_GNUC_INTERNAL gboolean _lrg_net_buffer_send (LrgNetBuffer *buffer, LrgNetMessage *message, GError **error);
G_GNUC_INTERNAL gboolean _lrg_net_buffer_flush (LrgNetBuffer *buffer, GSocket *socket, GError **error);
G_GNUC_INTERNAL LrgNetMessage *_lrg_net_buffer_receive (LrgNetBuffer *buffer, GSocket *socket, GError **error);
