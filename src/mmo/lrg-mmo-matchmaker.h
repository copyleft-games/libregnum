/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_MATCHMAKER (lrg_mmo_matchmaker_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoMatchmaker, lrg_mmo_matchmaker, LRG, MMO_MATCHMAKER, GObject)

/**
 * lrg_mmo_matchmaker_new:
 * @capacity: maximum queued accounts
 *
 * Creates an ephemeral FIFO matchmaking queue, owned by one thread.
 *
 * Returns: (transfer full): queue
 */
LRG_AVAILABLE_IN_ALL
LrgMmoMatchmaker *
lrg_mmo_matchmaker_new (guint capacity);

/**
 * lrg_mmo_matchmaker_enqueue:
 * @self: the service; confine to its owning thread
 * @account: authenticated unique account ID
 * @mode: game mode ID
 * @rating: skill rating
 * @now_us: nonnegative monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Queues one account for up to sixty seconds. Duplicates and capacity overflow fail.
 *
 * Returns: whether queued
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_matchmaker_enqueue (LrgMmoMatchmaker *self,
                            const gchar *account,
                            const gchar *mode,
                            guint rating,
                            gint64 now_us,
                            GError **error);

/**
 * lrg_mmo_matchmaker_cancel:
 * @self: the service; confine to its owning thread
 * @account: account ID
 *
 * Removes a queued account on disconnect or cancellation.
 */
LRG_AVAILABLE_IN_ALL
void
lrg_mmo_matchmaker_cancel (LrgMmoMatchmaker *self,
                           const gchar *account);

/**
 * lrg_mmo_matchmaker_take:
 * @self: the service; confine to its owning thread
 * @mode: game mode ID
 * @size: requested group size, 2 to 128
 * @spread: maximum difference between lowest and highest rating
 * @now_us: monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Expires old tickets and greedily packs FIFO tickets within the rating bound.
 * This bounded-policy queue does not optimize global match quality. Incomplete groups remain queued; no ticket is removed twice.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): account IDs, empty if no full group
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_mmo_matchmaker_take (LrgMmoMatchmaker *self,
                         const gchar *mode,
                         guint size,
                         guint spread,
                         gint64 now_us,
                         GError **error);

G_END_DECLS
