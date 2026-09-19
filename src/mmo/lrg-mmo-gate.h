/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_GATE (lrg_mmo_gate_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoGate, lrg_mmo_gate, LRG, MMO_GATE, GObject)

/**
 * lrg_mmo_gate_new:
 * @capacity: maximum tracked origins
 * @burst: positive request burst
 * @rate: finite positive refill per second
 *
 * Creates a bounded token-bucket admission gate. Use independently for IPs,
 * authentication attempts, account commands and chat. All calls share one owner thread.
 *
 * Returns: (transfer full): gate
 */
LRG_AVAILABLE_IN_ALL
LrgMmoGate *
lrg_mmo_gate_new (guint capacity,
                  guint burst,
                  gdouble rate);

/**
 * lrg_mmo_gate_admit:
 * @self: the service; confine to its owning thread
 * @origin: server-derived origin key, at most 128 bytes
 * @now_us: nonnegative monotonic server microseconds
 * @error: (nullable): return location for error
 *
 * Consumes one token, rejecting excessive requests and backwards clocks.
 * Idle entries expire after sixty seconds; a full origin table rejects new origins.
 *
 * Returns: whether admitted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_gate_admit (LrgMmoGate *self,
                    const gchar *origin,
                    gint64 now_us,
                    GError **error);

G_END_DECLS
