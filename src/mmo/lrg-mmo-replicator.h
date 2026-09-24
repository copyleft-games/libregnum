/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_REPLICATOR (lrg_mmo_replicator_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoReplicator, lrg_mmo_replicator, LRG, MMO_REPLICATOR, GObject)

/**
 * lrg_mmo_replicator_new:
 * @max_entities: maximum entities
 * @max_viewers: maximum viewers
 * @cell_size: finite positive spatial cell size, at most 1e100
 *
 * Creates a spatially indexed replicator owned by one thread.
 *
 * Returns: (transfer full): a replicator
 */
LRG_AVAILABLE_IN_ALL
LrgMmoReplicator *
lrg_mmo_replicator_new (guint max_entities,
                        guint max_viewers,
                        gdouble cell_size);

/**
 * lrg_mmo_replicator_upsert:
 * @self: the instance
 * @entity_id: nonzero stable entity ID
 * @zone: zone or instance ID
 * @x: world X
 * @y: world Y
 * @z: world Z
 * @state: public replicated state, at most 64 KiB
 * @error: (nullable): return location for an error
 *
 * Publishes authoritative state and increments its revision. All coordinates must be finite
 * and within one billion cells of the origin. Never include private account data.
 *
 * Returns: whether published
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_replicator_upsert (LrgMmoReplicator *self,
                           guint64 entity_id,
                           const gchar *zone,
                           gdouble x,
                           gdouble y,
                           gdouble z,
                           GBytes *state,
                           GError **error);

/**
 * lrg_mmo_replicator_remove:
 * @self: the instance
 * @entity_id: entity to despawn
 *
 * Removes an entity. A removal is sent to viewers that acknowledged it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_replicator_remove (LrgMmoReplicator *self,
                           guint64 entity_id);

/**
 * lrg_mmo_replicator_build:
 * @self: the instance
 * @viewer_id: nonzero connection-scoped viewer ID
 * @zone: visible zone or instance ID
 * @x: observer X
 * @y: observer Y
 * @z: observer Z
 * @radius: visible radius, between zero and four cell sizes
 * @error: (nullable): return location for an error
 *
 * Builds a delta against the acknowledged baseline. Returns the same pending delta
 * until acknowledged. The (ta(ttddday)at) result contains a sequence, updates
 * (entity ID, revision, X, Y, Z, bytes), and removed IDs, sorted by entity ID.
 * Deltas exceeding 1 MiB fail without changing the baseline. Apply updates/removals
 * atomically, then acknowledge; ignore duplicate sequences on the client.
 *
 * Returns: (transfer full) (nullable): delta, or NULL
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_replicator_build (LrgMmoReplicator *self,
                          guint64 viewer_id,
                          const gchar *zone,
                          gdouble x,
                          gdouble y,
                          gdouble z,
                          gdouble radius,
                          GError **error);

/**
 * lrg_mmo_replicator_acknowledge:
 * @self: the instance
 * @viewer_id: viewer ID
 * @sequence: exact pending sequence
 * @error: (nullable): return location for an error
 *
 * Advances a viewer baseline only for its pending sequence.
 *
 * Returns: whether acknowledged
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_replicator_acknowledge (LrgMmoReplicator *self,
                                guint64 viewer_id,
                                guint64 sequence,
                                GError **error);

/**
 * lrg_mmo_replicator_forget:
 * @self: the instance
 * @viewer_id: viewer ID
 *
 * Drops a disconnected viewer and its baseline. A reconnect requires an empty client
 * world and a fresh baseline. Sequence numbers are never reused within this replicator.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_replicator_forget (LrgMmoReplicator *self,
                           guint64 viewer_id);

/**
 * lrg_mmo_replicator_set_focus:
 * @self: the replicator
 * @viewer_id: connection-scoped viewer ID
 * @entity_id: the entity to send first, usually the viewer's own avatar, or 0 to clear
 *
 * Gives a viewer's pages a focus entity.  Whenever the focus entity changed and
 * is visible, lrg_mmo_replicator_build_page() places it on the page before the
 * round-robin of other changes, so under a tight budget in a crowded area the
 * viewer's own avatar is never delayed behind other entities.  The focus stays
 * outside the round-robin cursor.  lrg_mmo_replicator_forget() clears it.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_replicator_set_focus (LrgMmoReplicator *self,
                              guint64           viewer_id,
                              guint64           entity_id);

/**
 * lrg_mmo_replicator_build_page:
 * @self: the replicator
 * @viewer_id: connection-scoped viewer ID
 * @zone: visible zone
 * @x: viewer X
 * @y: viewer Y
 * @z: viewer Z
 * @radius: interest radius
 * @budget: conservative wire budget, 128 to 1048576 bytes
 * @more: (out): whether additional changes remain after this page
 * @error: (nullable): error return
 *
 * Builds a bandwidth-bounded delta. Removals precede updates; updates use ascending
 * entity ID. Only acknowledged page contents advance the baseline. Apply and
 * acknowledge every page, then build the next. Changed entities are chosen
 * round-robin across pages, after the viewer's focus entity (see
 * lrg_mmo_replicator_set_focus()); hosts choose per-viewer budgets and cadence.
 * A single entity larger than the budget fails without losing its update.
 *
 * Returns: (transfer full) (nullable): (ta(ttddday)at) page
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_replicator_build_page (LrgMmoReplicator *self, guint64 viewer_id, const gchar *zone,
                                         gdouble x, gdouble y, gdouble z, gdouble radius,
                                         guint budget, gboolean *more, GError **error);

G_END_DECLS
