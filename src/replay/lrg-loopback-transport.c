/* lrg-loopback-transport.c - In-process transport with simulated network faults
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-loopback-transport.h"
#include "../core/lrg-random-stream.h"

typedef struct
{
    gdouble  deliver_at;   /* network time the datagram becomes receivable */
    guint64  sequence;     /* send order, breaks delivery-time ties */
    guint    from;
    guint    to;
    GBytes  *payload;
} Datagram;

struct _LrgLoopbackNetwork
{
    GObject          parent_instance;

    gdouble          now;
    gdouble          latency;
    gdouble          jitter;
    gdouble          loss;
    gdouble          duplicate;
    guint64          next_sequence;
    guint            n_dropped;
    LrgRandomStream *random;     /* fault generator */
    GPtrArray       *in_flight;  /* Datagram* */
};

struct _LrgLoopbackTransport
{
    GObject             parent_instance;

    LrgLoopbackNetwork *network;  /* owned */
    guint               address;
};

static void lrg_loopback_transport_iface_init (LrgRollbackTransportInterface *iface);

G_DEFINE_FINAL_TYPE (LrgLoopbackNetwork, lrg_loopback_network, G_TYPE_OBJECT)
G_DEFINE_FINAL_TYPE_WITH_CODE (LrgLoopbackTransport, lrg_loopback_transport, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (LRG_TYPE_ROLLBACK_TRANSPORT,
                                                      lrg_loopback_transport_iface_init))

static void
datagram_free (Datagram *datagram)
{
    g_bytes_unref (datagram->payload);
    g_free (datagram);
}

/* ==========================================================================
 * LrgLoopbackNetwork
 * ========================================================================== */

static void
lrg_loopback_network_finalize (GObject *object)
{
    LrgLoopbackNetwork *self = LRG_LOOPBACK_NETWORK (object);

    g_clear_object (&self->random);
    g_clear_pointer (&self->in_flight, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_loopback_network_parent_class)->finalize (object);
}

static void
lrg_loopback_network_class_init (LrgLoopbackNetworkClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_loopback_network_finalize;
}

static void
lrg_loopback_network_init (LrgLoopbackNetwork *self)
{
    self->in_flight = g_ptr_array_new_with_free_func ((GDestroyNotify) datagram_free);
}

LrgLoopbackNetwork *
lrg_loopback_network_new (guint64 seed)
{
    LrgLoopbackNetwork *self;

    self = g_object_new (LRG_TYPE_LOOPBACK_NETWORK, NULL);
    self->random = lrg_random_stream_new (seed, 0x6c6f6f70);
    return self;
}

void
lrg_loopback_network_set_latency (LrgLoopbackNetwork *self,
                                  gdouble             latency,
                                  gdouble             jitter)
{
    g_return_if_fail (LRG_IS_LOOPBACK_NETWORK (self));
    g_return_if_fail (latency >= 0.0 && jitter >= 0.0);

    self->latency = latency;
    self->jitter = jitter;
}

void
lrg_loopback_network_set_loss (LrgLoopbackNetwork *self,
                               gdouble             loss,
                               gdouble             duplicate)
{
    g_return_if_fail (LRG_IS_LOOPBACK_NETWORK (self));
    g_return_if_fail (loss >= 0.0 && loss <= 1.0);
    g_return_if_fail (duplicate >= 0.0 && duplicate <= 1.0);

    self->loss = loss;
    self->duplicate = duplicate;
}

void
lrg_loopback_network_advance (LrgLoopbackNetwork *self,
                              gdouble             seconds)
{
    g_return_if_fail (LRG_IS_LOOPBACK_NETWORK (self));
    g_return_if_fail (seconds >= 0.0);

    self->now += seconds;
}

gdouble
lrg_loopback_network_get_time (LrgLoopbackNetwork *self)
{
    g_return_val_if_fail (LRG_IS_LOOPBACK_NETWORK (self), 0.0);

    return self->now;
}

guint
lrg_loopback_network_get_n_in_flight (LrgLoopbackNetwork *self)
{
    g_return_val_if_fail (LRG_IS_LOOPBACK_NETWORK (self), 0);

    return self->in_flight->len;
}

guint
lrg_loopback_network_get_n_dropped (LrgLoopbackNetwork *self)
{
    g_return_val_if_fail (LRG_IS_LOOPBACK_NETWORK (self), 0);

    return self->n_dropped;
}

static void
network_enqueue (LrgLoopbackNetwork *self,
                 guint               from,
                 guint               to,
                 GBytes             *payload)
{
    Datagram *datagram;

    datagram = g_new0 (Datagram, 1);
    datagram->deliver_at = self->now + self->latency +
                           self->jitter * lrg_random_stream_next_double (self->random);
    datagram->sequence = self->next_sequence++;
    datagram->from = from;
    datagram->to = to;
    datagram->payload = g_bytes_ref (payload);
    g_ptr_array_add (self->in_flight, datagram);
}

static void
network_send (LrgLoopbackNetwork *self,
              guint               from,
              guint               to,
              GBytes             *payload)
{
    /* Draw both faults for every datagram, so the fault sequence does not
     * depend on which earlier datagrams were dropped */
    gdouble drop_roll = lrg_random_stream_next_double (self->random);
    gdouble dup_roll = lrg_random_stream_next_double (self->random);

    if (drop_roll < self->loss)
    {
        self->n_dropped++;
        return;
    }
    network_enqueue (self, from, to, payload);
    if (dup_roll < self->duplicate)
        network_enqueue (self, from, to, payload);
}

static GBytes *
network_receive (LrgLoopbackNetwork *self,
                 guint               to,
                 guint              *from)
{
    Datagram *best = NULL;
    guint best_index = 0;
    GBytes *payload;
    guint i;

    /* The deliverable datagram for @to that arrives first */
    for (i = 0; i < self->in_flight->len; i++)
    {
        Datagram *datagram = g_ptr_array_index (self->in_flight, i);

        if (datagram->to != to || datagram->deliver_at > self->now)
            continue;
        if (best == NULL || datagram->deliver_at < best->deliver_at ||
            (datagram->deliver_at == best->deliver_at && datagram->sequence < best->sequence))
        {
            best = datagram;
            best_index = i;
        }
    }
    if (best == NULL)
        return NULL;

    *from = best->from;
    payload = g_bytes_ref (best->payload);
    g_ptr_array_remove_index_fast (self->in_flight, best_index);
    return payload;
}

LrgLoopbackTransport *
lrg_loopback_network_create_endpoint (LrgLoopbackNetwork *self,
                                      guint               address)
{
    LrgLoopbackTransport *transport;

    g_return_val_if_fail (LRG_IS_LOOPBACK_NETWORK (self), NULL);

    transport = g_object_new (LRG_TYPE_LOOPBACK_TRANSPORT, NULL);
    transport->network = g_object_ref (self);
    transport->address = address;
    return transport;
}

/* ==========================================================================
 * LrgLoopbackTransport
 * ========================================================================== */

static void
lrg_loopback_transport_send (LrgRollbackTransport *transport,
                             guint                 peer,
                             GBytes               *packet)
{
    LrgLoopbackTransport *self = LRG_LOOPBACK_TRANSPORT (transport);

    network_send (self->network, self->address, peer, packet);
}

static GBytes *
lrg_loopback_transport_receive (LrgRollbackTransport *transport,
                                guint                *peer)
{
    LrgLoopbackTransport *self = LRG_LOOPBACK_TRANSPORT (transport);

    return network_receive (self->network, self->address, peer);
}

static void
lrg_loopback_transport_iface_init (LrgRollbackTransportInterface *iface)
{
    iface->send = lrg_loopback_transport_send;
    iface->receive = lrg_loopback_transport_receive;
}

static void
lrg_loopback_transport_finalize (GObject *object)
{
    LrgLoopbackTransport *self = LRG_LOOPBACK_TRANSPORT (object);

    g_clear_object (&self->network);

    G_OBJECT_CLASS (lrg_loopback_transport_parent_class)->finalize (object);
}

static void
lrg_loopback_transport_class_init (LrgLoopbackTransportClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_loopback_transport_finalize;
}

static void
lrg_loopback_transport_init (LrgLoopbackTransport *self)
{
}
