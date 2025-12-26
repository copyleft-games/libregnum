# LrgNetServer

Multiplayer game server that listens for client connections and manages peer-to-peer messaging.

## Overview

`LrgNetServer` is a final GObject type that hosts a multiplayer game. It accepts client connections, tracks peer state, and facilitates message routing between peers. It operates in a non-blocking fashion with polling-based updates.

## Construction

```c
/* Bind to all interfaces on port 9999 */
LrgNetServer *server = lrg_net_server_new (NULL, 9999);

/* Bind to specific interface */
LrgNetServer *server = lrg_net_server_new ("192.168.1.100", 8080);
```

## Lifecycle

### Starting the Server

```c
g_autoptr(GError) error = NULL;

if (!lrg_net_server_start (server, &error))
{
    g_print ("Failed to start: %s\n", error->message);
    return FALSE;
}

g_print ("Server started\n");
```

### Stopping the Server

```c
lrg_net_server_stop (server);

g_assert_false (lrg_net_server_is_running (server));
```

### Checking Status

```c
if (lrg_net_server_is_running (server))
{
    g_print ("Server is running\n");
}
```

## Configuration

### Peer Limits

```c
/* Get max peers (0 = unlimited) */
guint max = lrg_net_server_get_max_peers (server);

/* Set max peers */
lrg_net_server_set_max_peers (server, 32);

/* No limit */
lrg_net_server_set_max_peers (server, 0);
```

### Host and Port

```c
const gchar *host = lrg_net_server_get_host (server);
guint port = lrg_net_server_get_port (server);

g_print ("Server: %s:%u\n", host, port);
```

## Peer Management

### Getting Connected Peers

```c
/* Get peer count */
guint count = lrg_net_server_get_peer_count (server);
g_print ("Connected peers: %u\n", count);

/* Get all peers */
GList *peers = lrg_net_server_get_peers (server);
for (GList *l = peers; l != NULL; l = l->next)
{
    LrgNetPeer *peer = (LrgNetPeer *)l->data;
    guint peer_id = lrg_net_peer_get_peer_id (peer);
    guint rtt = lrg_net_peer_get_rtt (peer);

    g_print ("Peer %u (RTT: %u ms)\n", peer_id, rtt);
}
g_list_free (peers);
```

### Getting Specific Peer

```c
LrgNetPeer *peer = lrg_net_server_get_peer (server, peer_id);

if (peer != NULL)
{
    const gchar *addr = lrg_net_peer_get_address (peer);
    guint port = lrg_net_peer_get_port (peer);
    g_print ("Peer at %s:%u\n", addr, port);
}
```

### Disconnecting Peers

```c
/* Disconnect specific peer */
lrg_net_server_disconnect_peer (server, peer_id);

/* Disconnect all peers */
lrg_net_server_disconnect_all (server);
```

## Messaging

### Sending to Specific Peer

```c
g_autoptr(GBytes) payload = g_bytes_new ("Hello", 5);
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,
    server_id,      /* sender */
    peer_id,        /* receiver */
    payload
);

g_autoptr(GError) error = NULL;
if (!lrg_net_server_send (server, peer_id, msg, &error))
{
    g_print ("Send failed: %s\n", error->message);
}
```

### Broadcasting to All Peers

```c
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,
    server_id,      /* sender */
    0,              /* broadcast to all */
    payload
);

if (!lrg_net_server_broadcast (server, msg, NULL))
{
    g_print ("Broadcast failed\n");
}
```

### Async Messaging

```c
/* Send asynchronously */
DexFuture *future = lrg_net_server_send_async (server, peer_id, msg);

/* In a fiber: */
/* if (!dex_await (future, error)) return; */

/* Or use callback: */
/* dex_future_then (future, on_send_complete, user_data, NULL); */
```

### Reliable Delivery

```c
/* Ensure message reaches peer even with packet loss */
lrg_net_message_set_reliable (msg, TRUE);

lrg_net_server_send (server, peer_id, msg, &error);
```

## Processing Events

### Polling

Must call `lrg_net_server_poll()` each frame to process network events:

```c
for (gint frame = 0; frame < num_frames; frame++)
{
    /* Process incoming connections and messages */
    lrg_net_server_poll (server);

    /* Game update */
    update_game ();

    /* Render */
    render_game ();
}
```

## Signals

```c
/* Server started listening */
g_signal_connect (server, "started", G_CALLBACK (on_server_started), NULL);

/* Server stopped */
g_signal_connect (server, "stopped", G_CALLBACK (on_server_stopped), NULL);

/* New peer connected */
g_signal_connect (server, "peer-connected",
                  G_CALLBACK (on_peer_connected), NULL);

/* Peer disconnected */
g_signal_connect (server, "peer-disconnected",
                  G_CALLBACK (on_peer_disconnected), NULL);

/* Message received from peer */
g_signal_connect (server, "message-received",
                  G_CALLBACK (on_message_received), NULL);
```

## Complete Example

```c
#include <libregnum.h>

static void
on_peer_connected (LrgNetServer *server,
                   LrgNetPeer   *peer,
                   gpointer      user_data)
{
    guint peer_id = lrg_net_peer_get_peer_id (peer);
    g_print ("Peer %u connected\n", peer_id);
}

static void
on_peer_disconnected (LrgNetServer *server,
                      guint32       peer_id,
                      const gchar  *reason,
                      gpointer      user_data)
{
    g_print ("Peer %u disconnected: %s\n", peer_id, reason);
}

static void
on_message_received (LrgNetServer    *server,
                     guint32          peer_id,
                     LrgNetMessage   *msg,
                     gpointer         user_data)
{
    GBytes *payload = lrg_net_message_get_payload (msg);
    if (payload != NULL)
    {
        gsize size;
        const guint8 *data = g_bytes_get_data (payload, &size);
        g_print ("Received %zu bytes from peer %u\n", size, peer_id);
    }
}

int main (void)
{
    g_autoptr(LrgNetServer) server = lrg_net_server_new (NULL, 9999);
    g_autoptr(GError) error = NULL;

    /* Connect signals */
    g_signal_connect (server, "peer-connected",
                      G_CALLBACK (on_peer_connected), NULL);
    g_signal_connect (server, "peer-disconnected",
                      G_CALLBACK (on_peer_disconnected), NULL);
    g_signal_connect (server, "message-received",
                      G_CALLBACK (on_message_received), NULL);

    /* Configure */
    lrg_net_server_set_max_peers (server, 32);

    /* Start */
    if (!lrg_net_server_start (server, &error))
    {
        g_print ("Error: %s\n", error->message);
        return 1;
    }

    g_print ("Server listening on port 9999\n");

    /* Run for 10 seconds */
    gint64 start = g_get_monotonic_time ();
    while (g_get_monotonic_time () - start < 10 * 1000000)
    {
        lrg_net_server_poll (server);
        g_usleep (16000);  /* ~60 FPS */
    }

    return 0;
}
```

## Error Handling

Server uses `LRG_NET_ERROR` domain:
- `LRG_NET_ERROR_CONNECTION_FAILED` - Could not start listening
- `LRG_NET_ERROR_SEND_FAILED` - Message send failed
- `LRG_NET_ERROR_PEER_NOT_FOUND` - Peer doesn't exist
