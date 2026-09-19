/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_MMO_STORE (lrg_mmo_store_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoStore, lrg_mmo_store, LRG, MMO_STORE, GObject)

/**
 * lrg_mmo_store_new:
 * @path: SQLite database path, or :memory:
 * @error: (nullable): return location for an error
 *
 * Opens a transactional record store with WAL and full synchronous commits.
 * Use on a worker thread for disk operations; each instance is confined to one thread.
 *
 * Returns: (transfer full) (nullable): a store, or NULL
 */
LRG_AVAILABLE_IN_ALL
LrgMmoStore *
lrg_mmo_store_new (const gchar *path,
                   GError **error);

/**
 * lrg_mmo_store_read:
 * @self: the instance
 * @key: record key
 * @revision: (out): current revision
 * @error: (nullable): return location for an error
 *
 * Reads a record. Missing keys return G_IO_ERROR_NOT_FOUND and revision zero.
 *
 * Returns: (transfer full) (nullable): stored bytes, or NULL
 */
LRG_AVAILABLE_IN_ALL
GBytes *
lrg_mmo_store_read (LrgMmoStore *self,
                    const gchar *key,
                    guint64 *revision,
                    GError **error);

/**
 * lrg_mmo_store_commit:
 * @self: the instance
 * @changes: (transfer none): a(stay) tuples of key, expected revision, bytes
 * @error: (nullable): return location for an error
 *
 * Atomically writes up to 256 distinct records, each at most 1 MiB.
 * Revision zero creates a missing record. Existing records require an exact revision.
 * Every successful write increments its revision. Any conflict rolls back the whole batch.
 *
 * Returns: whether committed
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_store_commit (LrgMmoStore *self,
                      GVariant *changes,
                      GError **error);

/**
 * lrg_mmo_store_commit_once:
 * @self: the store
 * @operation_id: stable unique operation ID, 1 to 256 UTF-8 bytes
 * @changes: (transfer none): a(stay) batch, identical on every retry
 * @duplicate: (out) (optional): whether the batch was previously committed
 * @error: (nullable): error return
 *
 * Commits a batch and durable receipt atomically. Exact retries succeed without
 * applying changes again, including after restart. Reusing an ID for different
 * changes fails. Retain the original batch (including revisions) when retrying.
 * Each successful new commit appends a digest-only audit entry; secrets are not logged.
 *
 * Returns: whether committed or recognized as an exact retry
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_store_commit_once (LrgMmoStore *self, const gchar *operation_id,
                                    GVariant *changes, gboolean *duplicate, GError **error);

/**
 * lrg_mmo_store_backup:
 * @self: the store
 * @path: destination path, which must not exist
 * @error: (nullable): error return
 *
 * Writes a consistent SQLite online backup, including receipts and audit data.
 * This blocking operation belongs on a worker thread. Existing files are never overwritten.
 *
 * Returns: whether copied successfully
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_store_backup (LrgMmoStore *self, const gchar *path, GError **error);

/**
 * lrg_mmo_store_read_audit:
 * @self: the store
 * @after: last seen sequence, or zero
 * @limit: maximum rows, 1 to 1000
 * @error: (nullable): error return
 *
 * Reads ordered (sequence, operation ID or empty string, batch digest, Unix seconds)
 * tuples. Audit history is durable and unpruned; deploy a retention/archive policy.
 *
 * Returns: (transfer full) (nullable): a(tssx) rows, or NULL
 */
LRG_AVAILABLE_IN_ALL
GVariant *lrg_mmo_store_read_audit (LrgMmoStore *self, guint64 after, guint limit, GError **error);

/**
 * lrg_mmo_store_commit_fenced:
 * @self: the store
 * @zone: zone lease ID
 * @owner: worker incarnation ID
 * @fence: expected fencing token
 * @changes: (transfer none): a(stay) batch
 * @error: (nullable): error return
 *
 * Checks the live shard lease inside the same write transaction as the record
 * batch. A stale worker cannot write after handoff or expiry. The host must route
 * every zone-owned write through this API rather than unrestricted commit().
 *
 * Returns: whether committed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_mmo_store_commit_fenced (LrgMmoStore *self, const gchar *zone, const gchar *owner,
                                      guint64 fence, GVariant *changes, GError **error);

G_END_DECLS
