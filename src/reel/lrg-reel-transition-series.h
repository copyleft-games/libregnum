/* lrg-reel-transition-series.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelTransitionSeries - an auto-sequencing clip that chains segments with
 * cross-fading transitions.
 *
 * A series is itself an #LrgReelClip; add segments with
 * lrg_reel_transition_series_add() and interpose transitions with
 * lrg_reel_transition_series_add_transition().  The series computes a unified
 * timeline where each transition overlaps the tail of the outgoing segment and
 * the head of the incoming segment by exactly @duration_in_frames frames,
 * pulling subsequent start positions earlier by that overlap amount.
 *
 * Valid construction alternates SEGMENT and TRANSITION items:
 *
 *   add (clip_A, 60)
 *   add_transition (fade, 15)
 *   add (clip_B, 90)
 *   add_transition (wipe, 20)
 *   add (clip_C, 45)
 *
 * The resulting timeline is:
 *
 *   frame   0 ..  59: clip_A solo
 *   frame  45 ..  59: clip_A outgoing + clip_B incoming (15-frame overlap)
 *   frame  45 .. 134: clip_B active (starts at 45, dur 90: frames [45,135))
 *   frame 115 .. 134: clip_B outgoing + clip_C incoming (20-frame overlap)
 *   frame 115 .. 159: clip_C active (starts at 115, dur 45: frames [115,160))
 *   total            = 115 + 45 = 160 frames
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-reel-clip.h"
#include "lrg-reel-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_TRANSITION_SERIES (lrg_reel_transition_series_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelTransitionSeries, lrg_reel_transition_series,
                      LRG, REEL_TRANSITION_SERIES, LrgReelClip)

/**
 * lrg_reel_transition_series_new:
 *
 * Creates a new, empty #LrgReelTransitionSeries.  Populate it with
 * lrg_reel_transition_series_add() and, optionally,
 * lrg_reel_transition_series_add_transition() between segments.
 *
 * Returns: (transfer full): a new #LrgReelTransitionSeries
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTransitionSeries *
lrg_reel_transition_series_new (void);

/**
 * lrg_reel_transition_series_add:
 * @self: an #LrgReelTransitionSeries
 * @clip: (transfer none): the #LrgReelClip to append as the next segment.
 * @duration_in_frames: how many frames this segment occupies (> 0).
 *
 * Appends @clip as a new segment lasting @duration_in_frames frames.  The
 * series takes a reference to @clip; the caller retains its own reference.
 * Call lrg_reel_transition_series_add_transition() before this to interpose a
 * transition between the previous segment and this one.
 *
 * Calling add() twice in a row (without add_transition() in between) creates a
 * hard-cut: the previous segment ends and this one begins on the same frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_transition_series_add (LrgReelTransitionSeries *self,
                                LrgReelClip             *clip,
                                gint                     duration_in_frames);

/**
 * lrg_reel_transition_series_add_transition:
 * @self: an #LrgReelTransitionSeries
 * @transition: (transfer none): the #LrgReelTransition to use.
 * @duration_in_frames: the overlap length in frames (>= 0).
 *
 * Inserts a transition to be applied between the previously added segment and
 * the next one added by lrg_reel_transition_series_add().  Must be called
 * after at least one add() call and before the next add() call.
 *
 * The @duration_in_frames frames are shared between the end of the outgoing
 * segment and the start of the incoming segment.  A value of 0 produces a
 * hard cut (identical to not calling this function).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_transition_series_add_transition (LrgReelTransitionSeries *self,
                                           LrgReelTransition       *transition,
                                           gint                     duration_in_frames);

/**
 * lrg_reel_transition_series_get_total_frames:
 * @self: an #LrgReelTransitionSeries
 *
 * Returns the total duration of the series in frames, accounting for all
 * segment durations and transition overlaps.  This value is also set as the
 * series' own clip duration so that parent containers see the correct length.
 *
 * Returns: total frame count (0 for an empty series)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reel_transition_series_get_total_frames (LrgReelTransitionSeries *self);

G_END_DECLS
