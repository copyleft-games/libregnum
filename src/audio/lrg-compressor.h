/* lrg-compressor.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A feed-forward, stereo-linked dynamic range compressor with a soft
 * knee: glue on a mix bus, punch on drums, evening out a bass line.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-audio-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_COMPRESSOR (lrg_compressor_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCompressor, lrg_compressor, LRG, COMPRESSOR, LrgAudioEffect)

/**
 * lrg_compressor_new:
 * @threshold_db: level in dBFS above which the gain is reduced (-60 .. 0)
 * @ratio: input dB over the threshold per output dB (1 .. 30)
 *
 * Creates a compressor; the other parameters (#LrgCompressor:knee,
 * #LrgCompressor:attack, #LrgCompressor:release, #LrgCompressor:makeup,
 * #LrgCompressor:mix) are GObject properties.
 *
 * Returns: (transfer full): a new #LrgCompressor
 */
LRG_AVAILABLE_IN_ALL
LrgCompressor * lrg_compressor_new (gdouble threshold_db,
                                    gdouble ratio);

/**
 * lrg_compressor_get_gain_reduction:
 * @self: an #LrgCompressor
 *
 * The gain reduction at the end of the last processed block, for a
 * meter.
 *
 * Returns: the reduction in dB (0 or negative)
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_compressor_get_gain_reduction (LrgCompressor *self);

G_END_DECLS
