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

G_END_DECLS
