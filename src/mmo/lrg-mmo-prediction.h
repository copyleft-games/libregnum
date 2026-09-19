/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <gio/gio.h>
#include "../lrg-version.h"

G_BEGIN_DECLS
#define LRG_TYPE_MMO_PREDICTION (lrg_mmo_prediction_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgMmoPrediction, lrg_mmo_prediction, LRG, MMO_PREDICTION, GObject)

/**
 * lrg_mmo_prediction_new:
 * @capacity: maximum pending inputs, 1 to 4096
 *
 * Creates a client movement prediction buffer. Deltas are game-defined movement
 * results, not trusted authority; server movement validation remains separate.
 *
 * Returns: (transfer full): buffer
 */
LRG_AVAILABLE_IN_ALL
LrgMmoPrediction *
lrg_mmo_prediction_new (guint capacity);

/**
 * lrg_mmo_prediction_push:
 * @self: the service; confine to its owning thread
 * @sequence: strictly increasing nonzero input sequence
 * @dx: predicted X displacement
 * @dy: predicted Y displacement
 * @dz: predicted Z displacement
 * @error: (nullable): return location for error
 *
 * Records a finite input delta, applying it to predicted position.
 *
 * Returns: whether accepted
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_prediction_push (LrgMmoPrediction *self,
                         guint64 sequence,
                         gdouble dx,
                         gdouble dy,
                         gdouble dz,
                         GError **error);

/**
 * lrg_mmo_prediction_reconcile:
 * @self: the service; confine to its owning thread
 * @acknowledged: last server-processed input, zero initially
 * @x: authoritative X
 * @y: authoritative Y
 * @z: authoritative Z
 * @error: (nullable): return location for error
 *
 * Discards acknowledged inputs and replays the remaining deltas on the authoritative
 * position. Future or backwards acknowledgments and nonfinite results are rejected atomically.
 *
 * Returns: whether reconciled
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_mmo_prediction_reconcile (LrgMmoPrediction *self,
                              guint64 acknowledged,
                              gdouble x,
                              gdouble y,
                              gdouble z,
                              GError **error);

/**
 * lrg_mmo_prediction_get_position:
 * @self: the service; confine to its owning thread
 *
 * Gets the current predicted position.
 *
 * Returns: (transfer full): (ddd) position
 */
LRG_AVAILABLE_IN_ALL
GVariant *
lrg_mmo_prediction_get_position (LrgMmoPrediction *self);

G_END_DECLS
