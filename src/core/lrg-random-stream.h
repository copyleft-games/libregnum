/* SPDX-License-Identifier: AGPL-3.0-or-later */
#pragma once
#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif
#include <glib-object.h>
#include "../lrg-version.h"
G_BEGIN_DECLS
#define LRG_TYPE_RANDOM_STREAM (lrg_random_stream_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRandomStream, lrg_random_stream, LRG, RANDOM_STREAM, GObject)

/**
 * lrg_random_stream_new:
 * @seed: initial seed
 * @sequence: stream selector (low 63 bits)
 *
 * Returns: (transfer full): a deterministic PCG32 stream
 */
LRG_AVAILABLE_IN_ALL
LrgRandomStream * lrg_random_stream_new (guint64 seed, guint64 sequence);

/**
 * lrg_random_stream_next_uint:
 * @self: a stream
 *
 * Returns: the next 32 random bits
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_random_stream_next_uint (LrgRandomStream *self);

/**
 * lrg_random_stream_advance:
 * @self: a stream
 * @draws: number of raw 32-bit outputs to skip
 *
 * Advances as if next_uint() had been called @draws times, without generating
 * the outputs. Zero leaves the stream unchanged. Runs in O(log @draws) time
 * with constant memory, including for the full guint64 range.
 *
 * Counts raw outputs, not higher-level calls: next_double() consumes two,
 * while bounded() may consume several because of rejection sampling.
 * The stream selector and snapshot format are unchanged.
 */
LRG_AVAILABLE_IN_ALL
void lrg_random_stream_advance (LrgRandomStream *self, guint64 draws);

/**
 * lrg_random_stream_bounded:
 * @self: a stream
 * @bound: positive exclusive upper bound
 *
 * Returns: an unbiased integer below @bound
 */
LRG_AVAILABLE_IN_ALL
guint32 lrg_random_stream_bounded (LrgRandomStream *self, guint32 bound);

/**
 * lrg_random_stream_next_double:
 * @self: a stream
 *
 * Returns: a uniform value in [0, 1), using 53 random bits
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_random_stream_next_double (LrgRandomStream *self);

/**
 * lrg_random_stream_snapshot:
 * @self: a stream
 *
 * Returns: (transfer full): a versioned portable state string
 */
LRG_AVAILABLE_IN_ALL
gchar * lrg_random_stream_snapshot (LrgRandomStream *self);

/**
 * lrg_random_stream_restore:
 * @self: a stream
 * @state: snapshot string
 * @error: (nullable): error location
 *
 * Restores atomically. Malformed or unknown versions leave state unchanged.
 *
 * Returns: whether restoration succeeded
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_random_stream_restore (LrgRandomStream *self, const gchar *state, GError **error);
G_END_DECLS
