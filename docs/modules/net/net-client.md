# LrgNetClient

Client for connecting to multiplayer servers and sending/receiving messages.

## Overview

`LrgNetClient` is a final GObject type that connects to a remote `LrgNetServer`. It manages the connection state, handles message delivery, and tracks network latency. Like the server, it operates in a non-blocking fashion with polling-based updates.

## Construction

```c
/* Create client */
LrgNetClient *client = lrg_net_client_new ("example.com", 9999);

/* With localhost */
LrgNetClient *client = lrg_net_client_new ("127.0.0.1", 9999);
```

## Connection Management

### Connecting

```c
g_autoptr(GError) error = NULL;

if (!lrg_net_client_connect (client, &error))
{
    g_print ("Failed to connect: %s\n", error->message);
    return FALSE;
}

g_print ("Connected! Local ID: %u\n",
         lrg_net_client_get_local_id (client));
```

### Async Connection

```c
DexFuture *future = lrg_net_client_connect_async (client);

/* In a fiber: */
/* if (!dex_await (future, error)) return; */

/* Or use callback: */
/* dex_future_then (future, on_connected, user_data, NULL); */
```

### Checking Connection Status

```c
if (lrg_net_client_is_connected (client))
{
    g_print ("Connected\n");
}
else
{
    g_print ("Not connected\n");
}
```

### Disconnecting

```c
lrg_net_client_disconnect (client);

g_assert_false (lrg_net_client_is_connected (client));
```

## Configuration

### Server Address

```c
const gchar *host = lrg_net_client_get_server_host (client);
guint port = lrg_net_client_get_server_port (client);

g_print ("Connecting to %s:%u\n", host, port);
```

### Local Peer ID

Assigned by the server upon successful connection.

```c
guint32 local_id = lrg_net_client_get_local_id (client);

if (local_id == 0)
{
    g_print ("Not connected yet\n");
}
else
{
    g_print ("Your peer ID: %u\n", local_id);
}
```

### Connection Timeout

```c
/* Default is 5000 ms */
guint timeout = lrg_net_client_get_timeout (client);

/* Set timeout */
lrg_net_client_set_timeout (client, 10000);  /* 10 seconds */
```

## Messaging

### Sending Messages

```c
g_autoptr(GBytes) payload = g_bytes_new (data, data_len);
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,
    lrg_net_client_get_local_id (client),  /* sender */
    0,  /* receiver (0 for server) */
    payload
);

g_autoptr(GError) error = NULL;
if (!lrg_net_client_send (client, msg, &error))
{
    g_print ("Send failed: %s\n", error->message);
}
```

### Async Sending

```c
DexFuture *future = lrg_net_client_send_async (client, msg);

/* In a fiber: */
/* if (!dex_await (future, error)) return; */
```

### Reliable Delivery

```c
/* Ensure message reaches server even with packet loss */
lrg_net_message_set_reliable (msg, TRUE);

lrg_net_client_send (client, msg, &error);
```

## Processing Events

### Polling

Must call `lrg_net_client_poll()` each frame to process network events:

```c
for (gint frame = 0; frame < num_frames; frame++)
{
    /* Process incoming messages */
    lrg_net_client_poll (client);

    /* Game update */
    update_game ();

    /* Render */
    render_game ();
}
```

## Signals

```c
/* Connected to server */
g_signal_connect (client, "connected", G_CALLBACK (on_connected), NULL);

/* Disconnected from server */
g_signal_connect (client, "disconnected", G_CALLBACK (on_disconnected), NULL);

/* Message received from server */
g_signal_connect (client, "message-received",
                  G_CALLBACK (on_message_received), NULL);

/* Connection attempt failed */
g_signal_connect (client, "connection-failed",
                  G_CALLBACK (on_connection_failed), NULL);
```

## Complete Example

```c
#include <libregnum.h>

static void
on_connected (LrgNetClient *client,
              gpointer      user_data)
{
    guint32 local_id = lrg_net_client_get_local_id (client);
    g_print ("Connected as peer %u\n", local_id);
}

static void
on_disconnected (LrgNetClient *client,
                 const gchar  *reason,
                 gpointer      user_data)
{
    g_print ("Disconnected: %s\n", reason);
}

static void
on_message_received (LrgNetClient    *client,
                     LrgNetMessage   *msg,
                     gpointer         user_data)
{
    guint32 sender = lrg_net_message_get_sender_id (msg);
    GBytes *payload = lrg_net_message_get_payload (msg);

    if (payload != NULL)
    {
        gsize size;
        const guint8 *data = g_bytes_get_data (payload, &size);
        g_print ("Received %zu bytes from peer %u\n", size, sender);
    }
}

static void
on_connection_failed (LrgNetClient *client,
                      GError       *error,
                      gpointer      user_data)
{
    g_print ("Connection failed: %s\n", error->message);
}

int main (void)
{
    g_autoptr(LrgNetClient) client = lrg_net_client_new ("127.0.0.1", 9999);
    g_autoptr(GError) error = NULL;

    /* Connect signals */
    g_signal_connect (client, "connected",
                      G_CALLBACK (on_connected), NULL);
    g_signal_connect (client, "disconnected",
                      G_CALLBACK (on_disconnected), NULL);
    g_signal_connect (client, "message-received",
                      G_CALLBACK (on_message_received), NULL);
    g_signal_connect (client, "connection-failed",
                      G_CALLBACK (on_connection_failed), NULL);

    /* Connect */
    if (!lrg_net_client_connect (client, &error))
    {
        g_print ("Connection error: %s\n", error->message);
        return 1;
    }

    /* Game loop */
    gint64 start = g_get_monotonic_time ();
    gint frame = 0;

    while (g_get_monotonic_time () - start < 30 * 1000000)  /* 30 seconds */
    {
        /* Process network events */
        lrg_net_client_poll (client);

        if (lrg_net_client_is_connected (client))
        {
            /* Send a message every second */
            if (frame % 60 == 0)
            {
                g_autofree gchar *data = g_strdup_printf ("Frame %d", frame);
                g_autoptr(GBytes) payload = g_bytes_new (data, strlen (data));
                g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
                    LRG_NET_MESSAGE_TYPE_DATA,
                    lrg_net_client_get_local_id (client),
                    0,  /* to server */
                    payload
                );

                if (!lrg_net_client_send (client, msg, &error))
                {
                    g_print ("Send error: %s\n", error->message);
                }
            }
        }

        g_usleep (16000);  /* ~60 FPS */
        frame++;
    }

    return 0;
}
```

## Error Handling

Client uses `LRG_NET_ERROR` domain:
- `LRG_NET_ERROR_CONNECTION_FAILED` - Could not connect to server
- `LRG_NET_ERROR_NOT_CONNECTED` - Operation requires active connection
- `LRG_NET_ERROR_TIMEOUT` - Connection timed out
