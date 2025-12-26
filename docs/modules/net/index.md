# Networking Module

The Networking module provides peer-to-peer multiplayer functionality with client-server architecture. It supports reliable and unreliable message delivery with latency tracking and connection state management.

## Overview

The Networking module consists of four main components:

- **LrgNetServer**: Hosts multiplayer games and manages peer connections
- **LrgNetClient**: Connects to multiplayer servers
- **LrgNetPeer**: Represents a connected peer with state and latency tracking
- **LrgNetMessage**: Serializable network message for peer communication

## Architecture

The system uses a client-server model:

```
Client 1 ───┐
            │
Client 2 ───┼─── Server ───┼─── All clients
            │
Client N ───┘
```

The server manages connections and routes messages between clients. Each peer maintains connection state and round-trip time (RTT) measurements.

## Quick Start

### Server Setup

```c
#include <libregnum.h>

int main (void)
{
    g_autoptr(LrgNetServer) server = lrg_net_server_new (NULL, 9999);
    g_autoptr(GError) error = NULL;

    /* Configure server */
    lrg_net_server_set_max_peers (server, 32);

    /* Start listening */
    if (!lrg_net_server_start (server, &error))
    {
        g_print ("Error: %s\n", error->message);
        return 1;
    }

    g_print ("Server listening on port 9999\n");

    /* Game loop */
    for (gint frame = 0; frame < num_frames; frame++)
    {
        /* Process network events */
        lrg_net_server_poll (server);

        /* Get connected peers */
        GList *peers = lrg_net_server_get_peers (server);
        g_print ("Connected peers: %u\n", g_list_length (peers));
        g_list_free (peers);
    }

    return 0;
}
```

### Client Setup

```c
#include <libregnum.h>

int main (void)
{
    g_autoptr(LrgNetClient) client = lrg_net_client_new ("localhost", 9999);
    g_autoptr(GError) error = NULL;

    /* Connect to server */
    if (!lrg_net_client_connect (client, &error))
    {
        g_print ("Error: %s\n", error->message);
        return 1;
    }

    g_print ("Connected to server, local ID: %u\n",
             lrg_net_client_get_local_id (client));

    /* Game loop */
    for (gint frame = 0; frame < num_frames; frame++)
    {
        /* Process network events */
        lrg_net_client_poll (client);

        if (lrg_net_client_is_connected (client))
        {
            /* Send message to server */
            g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
                LRG_NET_MESSAGE_TYPE_DATA,
                lrg_net_client_get_local_id (client),  /* sender */
                0,  /* receiver (0 for server) */
                NULL
            );

            if (!lrg_net_client_send (client, msg, &error))
            {
                g_print ("Send failed: %s\n", error->message);
            }
        }
    }

    return 0;
}
```

## Message Format

Messages are the fundamental unit of communication:

```c
/* Create a message */
g_autoptr(GBytes) payload = g_bytes_new (data, data_len);
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,  /* message type */
    sender_id,                  /* peer ID of sender */
    receiver_id,                /* peer ID of receiver (0 for broadcast) */
    payload                     /* optional payload */
);

/* Set reliability */
lrg_net_message_set_reliable (msg, TRUE);

/* Serialize for transmission */
g_autoptr(GBytes) serialized = lrg_net_message_serialize (msg);

/* Deserialize received message */
g_autoptr(GError) error = NULL;
g_autoptr(LrgNetMessage) received = lrg_net_message_deserialize (serialized, &error);
```

## Connection States

Peers have the following states:

- `LRG_NET_PEER_STATE_DISCONNECTED` - Not connected
- `LRG_NET_PEER_STATE_CONNECTING` - Connection in progress
- `LRG_NET_PEER_STATE_CONNECTED` - Actively connected
- `LRG_NET_PEER_STATE_DISCONNECTING` - Disconnection in progress

## Latency Measurement

Each peer tracks round-trip time (RTT):

```c
guint rtt_ms = lrg_net_peer_get_rtt (peer);

if (rtt_ms > 200)
{
    g_print ("High latency: %u ms\n", rtt_ms);
}
```

## Common Patterns

### Broadcasting Messages

```c
/* Send to all connected clients */
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA, server_id, 0, payload);

g_autoptr(GError) error = NULL;
if (!lrg_net_server_broadcast (server, msg, &error))
{
    g_print ("Broadcast failed: %s\n", error->message);
}
```

### Direct Peer-to-Peer

```c
/* Send to specific peer */
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA, client_id, peer_id, payload);

if (!lrg_net_client_send (client, msg, &error))
{
    g_print ("Send failed: %s\n", error->message);
}
```

### Async Operations

```c
/* Async server send */
DexFuture *future = lrg_net_server_send_async (server, peer_id, msg);
/* Use with dex_await() in fiber or dex_future_then() for callback */

/* Async client connect */
DexFuture *future = lrg_net_client_connect_async (client);

/* Async client send */
DexFuture *future = lrg_net_client_send_async (client, msg);
```

## Module Files

- `/var/home/zach/Source/Projects/libregnum/src/net/lrg-net-server.h` - Server implementation
- `/var/home/zach/Source/Projects/libregnum/src/net/lrg-net-client.h` - Client implementation
- `/var/home/zach/Source/Projects/libregnum/src/net/lrg-net-peer.h` - Peer representation
- `/var/home/zach/Source/Projects/libregnum/src/net/lrg-net-message.h` - Message format

## See Also

- [LrgNetServer Documentation](./net-server.md)
- [LrgNetClient Documentation](./net-client.md)
- [LrgNetPeer Documentation](./net-peer.md)
- [LrgNetMessage Documentation](./net-message.md)
