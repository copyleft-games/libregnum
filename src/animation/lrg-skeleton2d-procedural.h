/* lrg-skeleton2d-procedural.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSkeleton2DProcedural: procedural blend-shape layers (automatic
 * blinking at random intervals, breathing) that mix on top of whatever
 * clips posed an #LrgSkeleton2D.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-skeleton2d.h"

G_BEGIN_DECLS

#define LRG_TYPE_SKELETON2D_PROCEDURAL (lrg_skeleton2d_procedural_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSkeleton2DProcedural, lrg_skeleton2d_procedural, LRG, SKELETON2D_PROCEDURAL, GObject)

/**
 * lrg_skeleton2d_procedural_new:
 *
 * Creates an empty set of procedural layers.
 *
 * Returns: (transfer full): a new #LrgSkeleton2DProcedural
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
LrgSkeleton2DProcedural * lrg_skeleton2d_procedural_new (void);

/**
 * lrg_skeleton2d_procedural_add_blink:
 * @self: an #LrgSkeleton2DProcedural
 * @slot: the slot whose attachment has the eye shapes
 * @min_interval: shortest time from one blink's start to the next
 *   (seconds, more than 0)
 * @max_interval: longest such time (@min_interval or more)
 * @duration: how long one blink takes, closing and opening (seconds,
 *   more than 0); it is shortened to @min_interval if longer
 * @seed: seed of the interval generator: the same seed gives the same
 *   blinks on every run and platform
 *
 * Adds an automatic blink layer driving %LRG_SHAPE_BLINK_L and
 * %LRG_SHAPE_BLINK_R together.  Blink k starts after k + 1 intervals,
 * each drawn uniformly from [@min_interval, @max_interval]; during a
 * blink the weight rises linearly to 1 at its middle and falls back to
 * 0.  The layer only ever closes the eyes further: it sets each shape to
 * the larger of the clip's weight and its own.
 *
 * Returns: %TRUE on success, %FALSE if an argument is out of range
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_procedural_add_blink (LrgSkeleton2DProcedural *self,
                                              const gchar             *slot,
                                              gfloat                   min_interval,
                                              gfloat                   max_interval,
                                              gfloat                   duration,
                                              guint32                  seed);

/**
 * lrg_skeleton2d_procedural_add_breathe:
 * @self: an #LrgSkeleton2DProcedural
 * @slot: the slot whose attachment has the body shapes
 * @period: seconds per breath (more than 0)
 * @amount: the peak weight, 0 .. 1
 *
 * Adds a breathing layer driving %LRG_SHAPE_BREATHE with
 * `amount * (1 - cos (2 pi t / period)) / 2`: 0 at time 0, @amount
 * half a period later.  The layer adds to the clip's weight and clamps
 * the sum to 0 .. 1.
 *
 * Returns: %TRUE on success, %FALSE if an argument is out of range
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_skeleton2d_procedural_add_breathe (LrgSkeleton2DProcedural *self,
                                                const gchar             *slot,
                                                gfloat                   period,
                                                gfloat                   amount);

/**
 * lrg_skeleton2d_procedural_get_layer_count:
 * @self: an #LrgSkeleton2DProcedural
 *
 * Returns: the number of layers
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
guint lrg_skeleton2d_procedural_get_layer_count (LrgSkeleton2DProcedural *self);

/**
 * lrg_skeleton2d_procedural_clear:
 * @self: an #LrgSkeleton2DProcedural
 *
 * Removes every layer.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_procedural_clear (LrgSkeleton2DProcedural *self);

/**
 * lrg_skeleton2d_procedural_get_blink_weight:
 * @self: an #LrgSkeleton2DProcedural
 * @index: a layer index (a blink layer)
 * @time: time in seconds (0 or more)
 *
 * The weight a blink layer gives at @time (what
 * lrg_skeleton2d_procedural_apply() mixes in).  A pure function of the
 * layer's arguments and @time.
 *
 * Returns: the weight, 0 .. 1 (0 for an invalid @index or a layer that
 *   is not a blink)
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_skeleton2d_procedural_get_blink_weight (LrgSkeleton2DProcedural *self,
                                                   guint                    index,
                                                   gdouble                  time);

/**
 * lrg_skeleton2d_procedural_apply:
 * @self: an #LrgSkeleton2DProcedural
 * @skeleton: the skeleton, already posed by its clips
 * @time: time in seconds since the layers started (0 or more)
 *
 * Mixes every layer into @skeleton's slot blend-shape weights (blink:
 * the larger of the current weight and the layer's, breathe: the sum,
 * clamped).  Run it after the clips, before deforming or drawing.
 * Layers naming a slot the skeleton lacks are skipped.
 *
 * Since: 0.14
 */
LRG_AVAILABLE_IN_ALL
void lrg_skeleton2d_procedural_apply (LrgSkeleton2DProcedural *self,
                                      LrgSkeleton2D           *skeleton,
                                      gdouble                  time);

G_END_DECLS
