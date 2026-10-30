/* lrg-loopback-transport.h - In-process transport with simulated network faults
 *
 * Copyright 2026 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-rollback-transport.h"

G_BEGIN_DECLS

#define LRG_TYPE_LOOPBACK_NETWORK (lrg_loopback_network_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLoopbackNetwork, lrg_loopback_network, LRG, LOOPBACK_NETWORK, GObject)

#define LRG_TYPE_LOOPBACK_TRANSPORT (lrg_loopback_transport_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLoopbackTransport, lrg_loopback_transport, LRG, LOOPBACK_TRANSPORT, GObject)

/**
 * lrg_loopback_network_new:
 * @seed: seed of the fault generator (jitter and loss are reproducible)
 *
 * Creates a simulated network with its own clock. Datagrams sent through
 * its endpoints are delivered after the configured latency plus a random
 * jitter, may be dropped or duplicated, and arrive out of order when the
 * jitter exceeds the send interval. Time only moves with
 * lrg_loopback_network_advance(), so tests are deterministic.
 *
 * Returns: (transfer full): a new #LrgLoopbackNetwork
 *
 * Since: 0.4
 */
LRG_AVAILABLE_IN_ALL
LrgLoopbackNetwork *lrg_loopback_network_new (guint64 seed);

/**
 * lrg_loopback_network_set_latency:
 * @self: an #LrgLoopbackNetwork
 * @latency: one-way delay in seconds
 * @jitter: extra random delay, uniform in [0, @jitter) seconds
 *
 * Sets the delivery delay.
 */
LRG_AVAILABLE_IN_ALL
void lrg_loopback_network_set_latency (LrgLoopbackNetwork *self,
                                       gdouble             latency,
                                       gdouble             jitter);

/**
 * lrg_loopback_network_set_loss:
 * @self: an #LrgLoopbackNetwork
 * @loss: probability (0 to 1) that a datagram is dropped
 * @duplicate: probability (0 to 1) that a delivered datagram arrives twice
 *
 * Sets the fault rates.
 */
LRG_AVAILABLE_IN_ALL
void lrg_loopback_network_set_loss (LrgLoopbackNetwork *self,
                                    gdouble             loss,
                                    gdouble             duplicate);

/**
 * lrg_loopback_network_advance:
 * @self: an #LrgLoopbackNetwork
 * @seconds: time to advance the network clock by
 *
 * Advances the clock; datagrams whose delivery time has passed become
 * receivable.
 */
LRG_AVAILABLE_IN_ALL
void lrg_loopback_network_advance (LrgLoopbackNetwork *self,
                                   gdouble             seconds);

/**
 * lrg_loopback_network_get_time:
 * @self: an #LrgLoopbackNetwork
 *
 * Returns: the network clock, in seconds
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_loopback_network_get_time (LrgLoopbackNetwork *self);

/**
 * lrg_loopback_network_get_n_in_flight:
 * @self: an #LrgLoopbackNetwork
 *
 * Returns: datagrams sent and not yet received
 */
LRG_AVAILABLE_IN_ALL
guint lrg_loopback_network_get_n_in_flight (LrgLoopbackNetwork *self);

/**
 * lrg_loopback_network_get_n_dropped:
 * @self: an #LrgLoopbackNetwork
 *
 * Returns: datagrams dropped by the simulated loss so far
 */
LRG_AVAILABLE_IN_ALL
guint lrg_loopback_network_get_n_dropped (LrgLoopbackNetwork *self);

/**
 * lrg_loopback_network_create_endpoint:
 * @self: an #LrgLoopbackNetwork
 * @address: the endpoint's player index
 *
 * Creates the transport of player @address. Sending to player N delivers
 * to the endpoint created with address N.
 *
 * Returns: (transfer full): a new #LrgLoopbackTransport
 */
LRG_AVAILABLE_IN_ALL
LrgLoopbackTransport *lrg_loopback_network_create_endpoint (LrgLoopbackNetwork *self,
                                                            guint               address);

G_END_DECLS
