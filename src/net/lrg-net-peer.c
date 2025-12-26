/* lrg-net-peer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "net/lrg-net-peer.h"

/**
 * LrgNetPeer:
 *
 * Represents a connected peer in the network.
 */
struct _LrgNetPeer
{
    GObject          parent_instance;

    guint32          peer_id;
    gchar           *address;
    guint            port;
    LrgNetPeerState  state;
    guint            rtt;
    gint64           last_activity;
};

G_DEFINE_TYPE (LrgNetPeer, lrg_net_peer, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_PEER_ID,
    PROP_ADDRESS,
    PROP_PORT,
    PROP_STATE,
    PROP_RTT,
    PROP_LAST_ACTIVITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STATE_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_net_peer_finalize (GObject *object)
{
    LrgNetPeer *self = LRG_NET_PEER (object);

    g_clear_pointer (&self->address, g_free);

    G_OBJECT_CLASS (lrg_net_peer_parent_class)->finalize (object);
}

static void
lrg_net_peer_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgNetPeer *self = LRG_NET_PEER (object);

    switch (prop_id)
    {
    case PROP_PEER_ID:
        g_value_set_uint (value, self->peer_id);
        break;
    case PROP_ADDRESS:
        g_value_set_string (value, self->address);
        break;
    case PROP_PORT:
        g_value_set_uint (value, self->port);
        break;
    case PROP_STATE:
        g_value_set_enum (value, self->state);
        break;
    case PROP_RTT:
        g_value_set_uint (value, self->rtt);
        break;
    case PROP_LAST_ACTIVITY:
        g_value_set_int64 (value, self->last_activity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_peer_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgNetPeer *self = LRG_NET_PEER (object);

    switch (prop_id)
    {
    case PROP_PEER_ID:
        self->peer_id = g_value_get_uint (value);
        break;
    case PROP_ADDRESS:
        g_free (self->address);
        self->address = g_value_dup_string (value);
        break;
    case PROP_PORT:
        self->port = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_net_peer_class_init (LrgNetPeerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_net_peer_finalize;
    object_class->get_property = lrg_net_peer_get_property;
    object_class->set_property = lrg_net_peer_set_property;

    /**
     * LrgNetPeer:peer-id:
     *
     * The unique peer identifier.
     */
    properties[PROP_PEER_ID] =
        g_param_spec_uint ("peer-id",
                           "Peer ID",
                           "Unique peer identifier",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetPeer:address:
     *
     * The network address (IP or hostname).
     */
    properties[PROP_ADDRESS] =
        g_param_spec_string ("address",
                             "Address",
                             "Network address",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetPeer:port:
     *
     * The port number.
     */
    properties[PROP_PORT] =
        g_param_spec_uint ("port",
                           "Port",
                           "Port number",
                           0, 65535, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetPeer:state:
     *
     * The current connection state.
     */
    properties[PROP_STATE] =
        g_param_spec_enum ("state",
                           "State",
                           "Connection state",
                           LRG_TYPE_NET_PEER_STATE,
                           LRG_NET_PEER_STATE_DISCONNECTED,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetPeer:rtt:
     *
     * The round-trip time in milliseconds.
     */
    properties[PROP_RTT] =
        g_param_spec_uint ("rtt",
                           "RTT",
                           "Round-trip time in milliseconds",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgNetPeer:last-activity:
     *
     * Timestamp of last activity.
     */
    properties[PROP_LAST_ACTIVITY] =
        g_param_spec_int64 ("last-activity",
                            "Last Activity",
                            "Timestamp of last activity",
                            G_MININT64, G_MAXINT64, 0,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgNetPeer::state-changed:
     * @self: the #LrgNetPeer
     * @old_state: the previous state
     * @new_state: the new state
     *
     * Emitted when the peer's connection state changes.
     */
    signals[SIGNAL_STATE_CHANGED] =
        g_signal_new ("state-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      LRG_TYPE_NET_PEER_STATE,
                      LRG_TYPE_NET_PEER_STATE);
}

static void
lrg_net_peer_init (LrgNetPeer *self)
{
    self->state = LRG_NET_PEER_STATE_DISCONNECTED;
    self->rtt = 0;
    self->last_activity = g_get_real_time ();
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

/**
 * lrg_net_peer_new:
 * @peer_id: unique peer identifier
 * @address: network address (IP or hostname)
 * @port: port number
 *
 * Creates a new network peer.
 *
 * Returns: (transfer full): A new #LrgNetPeer
 */
LrgNetPeer *
lrg_net_peer_new (guint32      peer_id,
                  const gchar *address,
                  guint        port)
{
    return g_object_new (LRG_TYPE_NET_PEER,
                         "peer-id", peer_id,
                         "address", address,
                         "port", port,
                         NULL);
}

/**
 * lrg_net_peer_get_peer_id:
 * @self: an #LrgNetPeer
 *
 * Gets the unique peer identifier.
 *
 * Returns: The peer ID
 */
guint32
lrg_net_peer_get_peer_id (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), 0);
    return self->peer_id;
}

/**
 * lrg_net_peer_get_address:
 * @self: an #LrgNetPeer
 *
 * Gets the network address.
 *
 * Returns: (transfer none): The address string
 */
const gchar *
lrg_net_peer_get_address (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), NULL);
    return self->address;
}

/**
 * lrg_net_peer_get_port:
 * @self: an #LrgNetPeer
 *
 * Gets the port number.
 *
 * Returns: The port number
 */
guint
lrg_net_peer_get_port (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), 0);
    return self->port;
}

/**
 * lrg_net_peer_get_state:
 * @self: an #LrgNetPeer
 *
 * Gets the current connection state.
 *
 * Returns: The peer state
 */
LrgNetPeerState
lrg_net_peer_get_state (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), LRG_NET_PEER_STATE_DISCONNECTED);
    return self->state;
}

/**
 * lrg_net_peer_get_rtt:
 * @self: an #LrgNetPeer
 *
 * Gets the round-trip time in milliseconds.
 *
 * Returns: The RTT in ms, or 0 if unknown
 */
guint
lrg_net_peer_get_rtt (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), 0);
    return self->rtt;
}

/**
 * lrg_net_peer_get_last_activity:
 * @self: an #LrgNetPeer
 *
 * Gets the timestamp of last activity (microseconds since epoch).
 *
 * Returns: The last activity timestamp
 */
gint64
lrg_net_peer_get_last_activity (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), 0);
    return self->last_activity;
}

/**
 * lrg_net_peer_is_connected:
 * @self: an #LrgNetPeer
 *
 * Checks if the peer is currently connected.
 *
 * Returns: %TRUE if connected
 */
gboolean
lrg_net_peer_is_connected (LrgNetPeer *self)
{
    g_return_val_if_fail (LRG_IS_NET_PEER (self), FALSE);
    return self->state == LRG_NET_PEER_STATE_CONNECTED;
}

/**
 * lrg_net_peer_set_state:
 * @self: an #LrgNetPeer
 * @state: the new state
 *
 * Sets the connection state.
 *
 * This is intended for internal use by LrgNetServer/LrgNetClient.
 */
void
lrg_net_peer_set_state (LrgNetPeer      *self,
                        LrgNetPeerState  state)
{
    LrgNetPeerState old_state;

    g_return_if_fail (LRG_IS_NET_PEER (self));

    if (self->state == state)
        return;

    old_state = self->state;
    self->state = state;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);
    g_signal_emit (self, signals[SIGNAL_STATE_CHANGED], 0, old_state, state);
}

/**
 * lrg_net_peer_update_rtt:
 * @self: an #LrgNetPeer
 * @rtt_ms: round-trip time in milliseconds
 *
 * Updates the RTT measurement.
 */
void
lrg_net_peer_update_rtt (LrgNetPeer *self,
                         guint       rtt_ms)
{
    g_return_if_fail (LRG_IS_NET_PEER (self));

    if (self->rtt == rtt_ms)
        return;

    self->rtt = rtt_ms;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RTT]);
}

/**
 * lrg_net_peer_touch:
 * @self: an #LrgNetPeer
 *
 * Updates the last activity timestamp to now.
 */
void
lrg_net_peer_touch (LrgNetPeer *self)
{
    g_return_if_fail (LRG_IS_NET_PEER (self));

    self->last_activity = g_get_real_time ();
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LAST_ACTIVITY]);
}
