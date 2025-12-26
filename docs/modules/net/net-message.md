# LrgNetMessage

Serializable network message for peer-to-peer communication.

## Overview

`LrgNetMessage` is a boxed type that represents a single network message. Messages contain a type, sender/receiver IDs, optional payload, and metadata for reliable delivery and sequencing. They are the fundamental unit of communication in the networking system.

## Construction

```c
/* Simple message without payload */
LrgNetMessage *msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,
    sender_id,      /* peer ID of sender */
    receiver_id,    /* peer ID of receiver (0 = broadcast) */
    NULL            /* no payload */
);

/* With payload */
g_autoptr(GBytes) payload = g_bytes_new (data, data_len);
LrgNetMessage *msg = lrg_net_message_new (
    LRG_NET_MESSAGE_TYPE_DATA,
    sender_id,
    receiver_id,
    payload
);
```

## Message Types

```c
LRG_NET_MESSAGE_TYPE_DATA      /* Game data */
LRG_NET_MESSAGE_TYPE_PING      /* Latency measurement */
LRG_NET_MESSAGE_TYPE_PONG      /* Ping response */
LRG_NET_MESSAGE_TYPE_HANDSHAKE /* Connection establishment */
LRG_NET_MESSAGE_TYPE_DISCONNECT /* Disconnection notice */
```

## Addressing

### Sender and Receiver

```c
guint32 sender = lrg_net_message_get_sender_id (msg);
guint32 receiver = lrg_net_message_get_receiver_id (msg);

if (receiver == 0)
{
    g_print ("Broadcast message from peer %u\n", sender);
}
else
{
    g_print ("Direct message from %u to %u\n", sender, receiver);
}
```

### Broadcast Detection

```c
if (lrg_net_message_is_broadcast (msg))
{
    g_print ("This is a broadcast message\n");
}
```

## Payload

### Getting Payload

```c
GBytes *payload = lrg_net_message_get_payload (msg);

if (payload != NULL)
{
    gsize size;
    const guint8 *data = g_bytes_get_data (payload, &size);
    g_print ("Payload: %zu bytes\n", size);
}
else
{
    g_print ("No payload\n");
}
```

## Reliability

### Reliable Delivery

```c
/* Guarantee message delivery (retransmit on loss) */
lrg_net_message_set_reliable (msg, TRUE);

/* Best-effort delivery (no retransmit) */
lrg_net_message_set_reliable (msg, FALSE);

gboolean reliable = lrg_net_message_is_reliable (msg);
```

Use reliable delivery for important state (player position updates, game events). Use unreliable for frequent, loss-tolerant data (camera angle, animation frames).

## Sequencing

Messages can be sequenced for in-order delivery:

```c
/* Set sequence number */
lrg_net_message_set_sequence (msg, 42);

/* Get sequence number */
guint32 seq = lrg_net_message_get_sequence (msg);

/* Detect out-of-order messages */
if (seq < last_seq)
{
    g_print ("Out of order: %u < %u\n", seq, last_seq);
}
```

## Timestamps

```c
/* Get message creation timestamp (microseconds since epoch) */
gint64 timestamp = lrg_net_message_get_timestamp (msg);

gint64 now = g_get_monotonic_time ();
gint64 age_us = now - timestamp;
gdouble age_ms = age_us / 1000.0;

g_print ("Message age: %.2f ms\n", age_ms);
```

## Serialization

### Serializing for Transmission

```c
/* Convert to bytes for network transmission */
g_autoptr(GBytes) serialized = lrg_net_message_serialize (msg);

/* Send serialized data */
send_to_network (g_bytes_get_data (serialized, NULL),
                 g_bytes_get_size (serialized));
```

### Deserializing from Network

```c
/* Received raw bytes from network */
g_autoptr(GError) error = NULL;
g_autoptr(GBytes) raw = g_bytes_new (network_data, network_data_len);

g_autoptr(LrgNetMessage) received = lrg_net_message_deserialize (raw, &error);

if (received != NULL)
{
    /* Process message */
    guint32 sender = lrg_net_message_get_sender_id (received);
    g_print ("Message from peer %u\n", sender);
}
else
{
    g_print ("Deserialization failed: %s\n", error->message);
}
```

## Copying

```c
/* Create a deep copy */
g_autoptr(LrgNetMessage) copy = lrg_net_message_copy (msg);

/* Copy has same content but is independent */
lrg_net_message_set_reliable (copy, !lrg_net_message_is_reliable (msg));
```

## Memory Management

```c
/* Manual cleanup */
LrgNetMessage *msg = lrg_net_message_new (...);
/* use message */
lrg_net_message_free (msg);

/* Automatic cleanup */
g_autoptr(LrgNetMessage) msg = lrg_net_message_new (...);
/* automatically freed when scope ends */
```

## Complete Example

```c
#include <libregnum.h>

typedef struct
{
    gint32 x, y;
    guint8 action;
} PlayerInput;

void send_player_input (LrgNetClient *client, PlayerInput *input)
{
    g_autoptr(GBytes) payload = g_bytes_new (input, sizeof(*input));
    g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
        LRG_NET_MESSAGE_TYPE_DATA,
        lrg_net_client_get_local_id (client),
        0,  /* to server */
        payload
    );

    /* Reliable for gameplay */
    lrg_net_message_set_reliable (msg, TRUE);
    lrg_net_message_set_sequence (msg, ++g_sequence_number);

    g_autoptr(GError) error = NULL;
    if (!lrg_net_client_send (client, msg, &error))
    {
        g_print ("Send failed: %s\n", error->message);
    }
}

void receive_player_input (LrgNetMessage *msg)
{
    guint32 peer_id = lrg_net_message_get_sender_id (msg);
    GBytes *payload = lrg_net_message_get_payload (msg);

    if (payload == NULL)
        return;

    gsize size;
    const PlayerInput *input = (const PlayerInput *)g_bytes_get_data (payload, &size);

    if (size != sizeof(*input))
    {
        g_print ("Invalid payload size\n");
        return;
    }

    g_print ("Peer %u moved to (%d, %d), action=%u\n",
             peer_id, input->x, input->y, input->action);
}

int main (void)
{
    /* Test message creation */
    PlayerInput input = { 100, 200, 1 };
    g_autoptr(GBytes) payload = g_bytes_new (&input, sizeof(input));

    g_autoptr(LrgNetMessage) msg = lrg_net_message_new (
        LRG_NET_MESSAGE_TYPE_DATA,
        42,     /* sender */
        0,      /* broadcast */
        payload
    );

    lrg_net_message_set_reliable (msg, TRUE);
    lrg_net_message_set_sequence (msg, 1);

    /* Serialize */
    g_autoptr(GBytes) serialized = lrg_net_message_serialize (msg);
    g_print ("Serialized to %zu bytes\n", g_bytes_get_size (serialized));

    /* Deserialize */
    g_autoptr(GError) error = NULL;
    g_autoptr(LrgNetMessage) restored = lrg_net_message_deserialize (serialized, &error);

    if (restored != NULL)
    {
        g_print ("Deserialized successfully\n");
        g_print ("  Type: %d\n", lrg_net_message_get_message_type (restored));
        g_print ("  From: %u\n", lrg_net_message_get_sender_id (restored));
        g_print ("  To: %u\n", lrg_net_message_get_receiver_id (restored));
        g_print ("  Reliable: %s\n", lrg_net_message_is_reliable (restored) ? "yes" : "no");
        g_print ("  Sequence: %u\n", lrg_net_message_get_sequence (restored));
    }

    return 0;
}
```

## Type Information

```c
GType type = lrg_net_message_get_type ();
g_assert_true (G_TYPE_IS_BOXED (type));
g_assert_cmpstr (g_type_name (type), ==, "LrgNetMessage");
```
