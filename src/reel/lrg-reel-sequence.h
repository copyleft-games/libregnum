/* lrg-reel-sequence.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSequence - a clip that time-shifts, sequences, loops, or freezes a
 * group of child clips.
 *
 * A sequence is itself an #LrgReelClip, so it stacks and nests like any layer.
 * Its #LrgReelSequenceMode decides how the parent's relative frame is mapped
 * onto the frame its children observe:
 *
 * - %LRG_REEL_SEQUENCE_MODE_SHIFT: children share the sequence's own window
 *   (offset by from-frame, bounded by duration).
 * - %LRG_REEL_SEQUENCE_MODE_SERIES: children play back-to-back, each occupying
 *   its own duration-in-frames.
 * - %LRG_REEL_SEQUENCE_MODE_LOOP: children repeat every loop-frames, for an
 *   optional number of times.
 * - %LRG_REEL_SEQUENCE_MODE_FREEZE: children are locked to a fixed frame.
 *
 * Timing contract: the caller of a clip's render (the renderer for top-level
 * clips, or a sequence for its children) pushes that clip's (from, duration)
 * window onto the #LrgReelContext before calling, and pops it afterward.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-clip.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SEQUENCE (lrg_reel_sequence_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelSequence, lrg_reel_sequence, LRG, REEL_SEQUENCE, LrgReelClip)

/**
 * lrg_reel_sequence_new:
 * @from: parent-relative start frame.
 * @duration_in_frames: window length, or %LRG_REEL_DURATION_INFINITE.
 *
 * Creates a %LRG_REEL_SEQUENCE_MODE_SHIFT sequence: children are time-shifted
 * by @from and bounded by @duration_in_frames.
 *
 * Returns: (transfer full): a new #LrgReelSequence
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSequence *
lrg_reel_sequence_new (gint from,
                       gint duration_in_frames);

/**
 * lrg_reel_sequence_new_series:
 *
 * Creates a %LRG_REEL_SEQUENCE_MODE_SERIES sequence.  Children play
 * back-to-back; each child occupies its own duration-in-frames.  The
 * sequence's total duration is the sum of its children's durations.
 *
 * Returns: (transfer full): a new #LrgReelSequence
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSequence *
lrg_reel_sequence_new_series (void);

/**
 * lrg_reel_sequence_new_loop:
 * @loop_frames: the period of one iteration in frames (> 0).
 * @times: number of iterations, or <= 0 for infinite.
 *
 * Creates a %LRG_REEL_SEQUENCE_MODE_LOOP sequence: children repeat every
 * @loop_frames frames.
 *
 * Returns: (transfer full): a new #LrgReelSequence
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSequence *
lrg_reel_sequence_new_loop (gint loop_frames,
                            gint times);

/**
 * lrg_reel_sequence_new_freeze:
 * @frozen_frame: the frame children are locked to.
 *
 * Creates a %LRG_REEL_SEQUENCE_MODE_FREEZE sequence: children always observe
 * @frozen_frame regardless of the real frame.
 *
 * Returns: (transfer full): a new #LrgReelSequence
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSequence *
lrg_reel_sequence_new_freeze (gint frozen_frame);

/**
 * lrg_reel_sequence_add_child:
 * @self: a #LrgReelSequence
 * @child: (transfer none): a child #LrgReelClip (may itself be a sequence).
 *
 * Appends @child.  In series mode the sequence's duration is recomputed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_sequence_add_child (LrgReelSequence *self,
                             LrgReelClip     *child);

LRG_AVAILABLE_IN_ALL
LrgReelSequenceMode
lrg_reel_sequence_get_mode (LrgReelSequence *self);

LRG_AVAILABLE_IN_ALL
guint
lrg_reel_sequence_get_n_children (LrgReelSequence *self);

G_END_DECLS
