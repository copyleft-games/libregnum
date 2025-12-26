# LrgNetPeer

Represents a connected peer in the network, tracking connection state and latency.

## Overview

`LrgNetPeer` is a final GObject type that represents a remote peer on a network. It stores the peer's identification, address, port, connection state, and round-trip time (RTT) measurement. Peers are typically managed by the server and accessed through it.

## Construction

```c
/* Create a peer (usually done by server/client internally) */
LrgNetPeer *peer = lrg_net_peer_new (peer_id, "192.168.1.100", 54321);
```

## Properties

### Identification

```c
/* Unique peer ID assigned by server */
guint32 peer_id = lrg_net_peer_get_peer_id (peer);

/* Network address */
const gchar *addr = lrg_net_peer_get_address (peer);

/* Network port */
guint port = lrg_net_peer_get_port (peer);

g_print ("Peer %u at %s:%u\n", peer_id, addr, port);
```

## Connection State

### State Enum

States represent the connection lifecycle:

- `LRG_NET_PEER_STATE_DISCONNECTED` - No connection
- `LRG_NET_PEER_STATE_CONNECTING` - Connection in progress
- `LRG_NET_PEER_STATE_CONNECTED` - Actively connected
- `LRG_NET_PEER_STATE_DISCONNECTING` - Disconnection in progress

### Getting State

```c
LrgNetPeerState state = lrg_net_peer_get_state (peer);

switch (state)
{
    case LRG_NET_PEER_STATE_CONNECTED:
        g_print ("Peer is connected\n");
        break;
    case LRG_NET_PEER_STATE_DISCONNECTED:
        g_print ("Peer is disconnected\n");
        break;
    default:
        g_print ("Peer state: %d\n", state);
        break;
}
```

### Convenience Check

```c
if (lrg_net_peer_is_connected (peer))
{
    g_print ("Peer is ready for communication\n");
}
```

### Setting State

Internal use by server/client:

```c
/* Server changes peer state on connect */
lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_CONNECTED);

/* When disconnecting */
lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_DISCONNECTING);
```

## Latency Measurement

### Getting RTT

Round-trip time in milliseconds.

```c
guint rtt_ms = lrg_net_peer_get_rtt (peer);

if (rtt_ms == 0)
{
    g_print ("RTT not measured yet\n");
}
else if (rtt_ms < 50)
{
    g_print ("Excellent connection: %u ms\n", rtt_ms);
}
else if (rtt_ms < 100)
{
    g_print ("Good connection: %u ms\n", rtt_ms);
}
else if (rtt_ms < 200)
{
    g_print ("Fair connection: %u ms\n", rtt_ms);
}
else
{
    g_print ("Poor connection: %u ms\n", rtt_ms);
}
```

### Updating RTT

Internal use by server/client after ping/pong:

```c
lrg_net_peer_update_rtt (peer, 35);  /* 35 ms */
```

## Activity Tracking

### Last Activity Timestamp

Tracks when the peer last communicated.

```c
gint64 last_activity = lrg_net_peer_get_last_activity (peer);
gint64 now = g_get_monotonic_time ();

gint64 idle_us = now - last_activity;
gdouble idle_sec = idle_us / 1000000.0;

if (idle_sec > 30.0)
{
    g_print ("Peer idle for %.1f seconds\n", idle_sec);
}
```

### Touching Activity

Update last activity to now:

```c
lrg_net_peer_touch (peer);
```

Used by server when receiving data from peer.

## Signals

### State Change

```c
static void
on_peer_state_changed (LrgNetPeer   *peer,
                       LrgNetPeerState old_state,
                       LrgNetPeerState new_state,
                       gpointer      user_data)
{
    guint32 peer_id = lrg_net_peer_get_peer_id (peer);
    g_print ("Peer %u state: %d -> %d\n", peer_id, old_state, new_state);
}

g_signal_connect (peer, "state-changed",
                  G_CALLBACK (on_peer_state_changed), NULL);
```

## Complete Example

```c
#include <libregnum.h>

typedef struct
{
    LrgNetPeer *peer;
    guint      disconnection_timeout;
} PeerWatcher;

static gboolean
check_peer_health (gpointer user_data)
{
    PeerWatcher *watcher = (PeerWatcher *)user_data;
    LrgNetPeer *peer = watcher->peer;

    guint rtt = lrg_net_peer_get_rtt (peer);
    gint64 now = g_get_monotonic_time ();
    gint64 last_activity = lrg_net_peer_get_last_activity (peer);
    gdouble idle_sec = (now - last_activity) / 1000000.0;

    g_print ("Peer %u: RTT=%u ms, Idle=%.1f sec, State=%d\n",
             lrg_net_peer_get_peer_id (peer),
             rtt,
             idle_sec,
             lrg_net_peer_get_state (peer));

    /* Timeout if idle too long */
    if (idle_sec > watcher->disconnection_timeout)
    {
        g_print ("Peer %u timed out\n", lrg_net_peer_get_peer_id (peer));
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

int main (void)
{
    g_autoptr(LrgNetPeer) peer = lrg_net_peer_new (42, "192.168.1.100", 54321);

    /* Simulate connection state changes */
    lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_CONNECTING);
    g_assert_false (lrg_net_peer_is_connected (peer));

    g_usleep (100000);
    lrg_net_peer_set_state (peer, LRG_NET_PEER_STATE_CONNECTED);
    g_assert_true (lrg_net_peer_is_connected (peer));

    /* Simulate activity and latency */
    lrg_net_peer_update_rtt (peer, 25);
    lrg_net_peer_touch (peer);

    g_usleep (500000);  /* 500 ms delay */
    lrg_net_peer_touch (peer);

    guint rtt = lrg_net_peer_get_rtt (peer);
    g_print ("Peer %u: RTT=%u ms, Connected=%d\n",
             lrg_net_peer_get_peer_id (peer),
             rtt,
             lrg_net_peer_is_connected (peer));

    return 0;
}
```

## Server Integration

Typically used through the server:

```c
LrgNetServer *server = lrg_net_server_new (NULL, 9999);
/* ... start server ... */

/* Get peer from server */
LrgNetPeer *peer = lrg_net_server_get_peer (server, peer_id);

if (peer != NULL)
{
    guint rtt = lrg_net_peer_get_rtt (peer);
    gboolean connected = lrg_net_peer_is_connected (peer);

    g_print ("Peer %u: connected=%d, rtt=%u ms\n",
             lrg_net_peer_get_peer_id (peer),
             connected,
             rtt);
}
```
