/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#include "lrg-net-message.h"
#include <gio/gio.h>

/*
 * stats_peer: the LrgNetStats peer key ("address:port") traffic through
 * this buffer is counted under; NULL until the owner names the peer.
 */
typedef struct
{
    GByteArray *input;
    GByteArray *output;
    gchar      *stats_peer;
} LrgNetBuffer;

G_GNUC_INTERNAL void _lrg_net_buffer_init (LrgNetBuffer *buffer);
G_GNUC_INTERNAL void _lrg_net_buffer_clear (LrgNetBuffer *buffer);
G_GNUC_INTERNAL void _lrg_net_buffer_set_stats_peer (LrgNetBuffer *buffer, const gchar *peer);
G_GNUC_INTERNAL gboolean _lrg_net_buffer_send (LrgNetBuffer *buffer, LrgNetMessage *message, GError **error);
G_GNUC_INTERNAL gboolean _lrg_net_buffer_flush (LrgNetBuffer *buffer, GOutputStream *output, GError **error);
G_GNUC_INTERNAL LrgNetMessage *_lrg_net_buffer_receive (LrgNetBuffer *buffer, GInputStream *input, GError **error);

/*
 * LrgNetStats hooks (lrg-net-stats.c). Each feeds the default
 * LrgNetStats and returns at once while it is disabled (one atomic
 * read). Used by src/net and src/mmo; not public API.
 */
G_GNUC_INTERNAL gboolean _lrg_net_stats_hooks_active (void);
G_GNUC_INTERNAL void _lrg_net_stats_hook_message (const gchar *peer, const LrgNetMessage *message,
                                                  gsize bytes, gboolean sent);
G_GNUC_INTERNAL void _lrg_net_stats_hook_frame (const gchar *peer, const gchar *message_type,
                                                gsize bytes, gboolean sent);
G_GNUC_INTERNAL void _lrg_net_stats_hook_peer (const gchar *peer, gboolean connected);
G_GNUC_INTERNAL void _lrg_net_stats_hook_rtt (const gchar *peer, gdouble rtt_ms);
G_GNUC_INTERNAL void _lrg_net_stats_hook_lost (const gchar *peer, gint count);
