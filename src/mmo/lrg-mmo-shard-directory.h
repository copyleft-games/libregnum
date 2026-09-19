/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
#include "lrg-mmo-store.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_SHARD_DIRECTORY (lrg_mmo_shard_directory_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoShardDirectory, lrg_mmo_shard_directory, LRG, MMO_SHARD_DIRECTORY, GObject)

/**
 * lrg_mmo_shard_directory_new:
 * @store: transactional store, retained by the service
 * @error: (nullable): return location for error
 *
 * Creates a directory backed by the store database. Workers sharing this database
 * coordinate through SQLite transactions. All deadlines use database Unix time.
 *
 * Returns: (transfer full) (nullable): directory
 */
LRG_AVAILABLE_IN_ALL
LrgMmoShardDirectory *
lrg_mmo_shard_directory_new (LrgMmoStore *store,
                             GError **error);

/**
 * lrg_mmo_shard_directory_acquire:
 * @self: the service; confine to its owning thread
 * @zone: zone ID, 1 to 64 ASCII identifier characters
 * @owner: unique worker incarnation ID; never reuse after restart
 * @endpoint: advertised endpoint, at most 512 UTF-8 bytes
 * @ttl: lease seconds, 1 to 86400
 * @error: (nullable): return location for error
 *
 * Acquires an absent or expired zone and increments its durable fencing token.
 * A live lease cannot be acquired, even by the same owner; renew it explicitly.
 *
 * Returns: nonzero fence, or zero on error
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_shard_directory_acquire (LrgMmoShardDirectory *self,
                                 const gchar *zone,
                                 const gchar *owner,
                                 const gchar *endpoint,
                                 guint ttl,
                                 GError **error);

/**
 * lrg_mmo_shard_directory_renew:
 * @self: the service; confine to its owning thread
 * @zone: zone ID, 1 to 64 ASCII identifier characters
 * @owner: unique worker incarnation ID; never reuse after restart
 * @fence: expected ownership fencing token
 * @ttl: lease seconds, 1 to 86400
 * @error: (nullable): return location for error
 *
 * Renews an unexpired lease only for its current owner and fence.
 *
 * Returns: whether renewed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_shard_directory_renew (LrgMmoShardDirectory *self,
                               const gchar *zone,
                               const gchar *owner,
                               guint64 fence,
                               guint ttl,
                               GError **error);

/**
 * lrg_mmo_shard_directory_release:
 * @self: the service; confine to its owning thread
 * @zone: zone ID, 1 to 64 ASCII identifier characters
 * @owner: unique worker incarnation ID; never reuse after restart
 * @fence: expected ownership fencing token
 * @error: (nullable): return location for error
 *
 * Expires the matching lease without resetting its fencing history.
 *
 * Returns: whether released
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_shard_directory_release (LrgMmoShardDirectory *self,
                                 const gchar *zone,
                                 const gchar *owner,
                                 guint64 fence,
                                 GError **error);

/**
 * lrg_mmo_shard_directory_handoff:
 * @self: the service; confine to its owning thread
 * @zone: zone ID, 1 to 64 ASCII identifier characters
 * @owner: unique worker incarnation ID; never reuse after restart
 * @fence: expected ownership fencing token
 * @destination: destination worker incarnation
 * @endpoint: destination endpoint
 * @state: authoritative handoff snapshot, at most 1 MiB
 * @ttl: destination lease seconds
 * @error: (nullable): return location for error
 *
 * Atomically persists a handoff snapshot, changes ownership and increments the fence.
 * The previous owner immediately loses permission to commit fenced state.
 *
 * Returns: new fence, or zero on error
 */
LRG_AVAILABLE_IN_ALL
guint64
lrg_mmo_shard_directory_handoff (LrgMmoShardDirectory *self,
                                 const gchar *zone,
                                 const gchar *owner,
                                 guint64 fence,
                                 const gchar *destination,
                                 const gchar *endpoint,
                                 GBytes *state,
                                 guint ttl,
                                 GError **error);

/**
 * lrg_mmo_shard_directory_lookup:
 * @self: the service; confine to its owning thread
 * @zone: zone ID, 1 to 64 ASCII identifier characters
 * @error: (nullable): return location for error
 *
 * Returns a live lease as (fence, owner, endpoint, expiry seconds, handoff bytes).
 *
 * Returns: (transfer full) (nullable): (tssxay) lease, or NULL
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_shard_directory_lookup (LrgMmoShardDirectory *self,
                                const gchar *zone,
                                GError **error);

G_END_DECLS
