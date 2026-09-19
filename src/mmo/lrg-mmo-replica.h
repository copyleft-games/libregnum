/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_REPLICA (lrg_mmo_replica_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoReplica, lrg_mmo_replica, LRG, MMO_REPLICA, GObject)

/**
 * lrg_mmo_replica_new:
 * @capacity: maximum client entities
 *
 * Creates an empty client replica.
 *
 * Returns: (transfer full): replica
 */
LRG_AVAILABLE_IN_ALL
LrgMmoReplica *
lrg_mmo_replica_new (guint capacity);

/**
 * lrg_mmo_replica_reset:
 * @self: the service; confine to its owning thread
 * @stream: nonempty server stream identity
 *
 * Explicitly clears the world and sequence history when changing server streams.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_replica_reset (LrgMmoReplica *self,
                       const gchar *stream);

/**
 * lrg_mmo_replica_apply:
 * @self: the service; confine to its owning thread
 * @stream: expected server stream identity
 * @delta: (transfer none): (ta(ttddday)at) server delta
 * @error: (nullable): return location for error
 *
 * Validates the entire bounded delta before applying it atomically. Exact duplicates
 * succeed; changed duplicates, stale sequences, duplicate IDs and invalid numbers fail.
 *
 * Returns: whether applied or an exact retry
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_replica_apply (LrgMmoReplica *self,
                       const gchar *stream,
                       GVariant *delta,
                       GError **error);

/**
 * lrg_mmo_replica_lookup:
 * @self: the service; confine to its owning thread
 * @entity_id: entity ID
 *
 * Looks up current authoritative state.
 *
 * Returns: (transfer full) (nullable): (tddday) revision, position and state
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_replica_lookup (LrgMmoReplica *self,
                        guint64 entity_id);

/**
 * lrg_mmo_replica_interpolate:
 * @self: the service; confine to its owning thread
 * @entity_id: entity ID
 * @alpha: interpolation fraction between zero and one
 *
 * Interpolates previous and current authoritative positions without changing simulation state.
 *
 * Returns: (transfer full) (nullable): (ddd) rendered position
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_replica_interpolate (LrgMmoReplica *self,
                             guint64 entity_id,
                             gdouble alpha);

/**
 * lrg_mmo_replica_get_sequence:
 * @self: the service; confine to its owning thread
 *
 * Gets the last completely applied sequence, suitable for acknowledgment.
 *
 * Returns: sequence or zero
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_replica_get_sequence (LrgMmoReplica *self);

G_END_DECLS
