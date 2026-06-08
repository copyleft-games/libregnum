/* lrg-reel-context.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelContext - the per-frame rendering context.
 *
 * Carries the frame currently being rendered along with the composition's
 * dimensions and frame rate, and maintains an offset stack so that nested
 * sequences observe a frame relative to their own start.  lrg_reel_context_
 * get_frame() returns the absolute frame minus the sum of pushed offsets.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

/**
 * LRG_REEL_DURATION_INFINITE:
 *
 * Sentinel duration meaning "no upper bound" (e.g. a clip that lasts the whole
 * reel).  Used for window durations in #LrgReelContext and clip/sequence
 * timing.
 *
 * Since: 1.0
 */
#define LRG_REEL_DURATION_INFINITE (G_MAXINT)

#define LRG_TYPE_REEL_CONTEXT (lrg_reel_context_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelContext, lrg_reel_context, LRG, REEL_CONTEXT, GObject)

/**
 * lrg_reel_context_new:
 * @absolute_frame: the frame being rendered.
 * @fps: frames per second (> 0).
 * @width: composition width in pixels.
 * @height: composition height in pixels.
 * @duration_in_frames: total composition duration in frames.
 *
 * Creates a render context with an empty offset stack.
 *
 * Returns: (transfer full): a new #LrgReelContext
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelContext *
lrg_reel_context_new (gint    absolute_frame,
                      gdouble fps,
                      gint    width,
                      gint    height,
                      gint    duration_in_frames);

/**
 * lrg_reel_context_get_frame:
 * @self: a #LrgReelContext
 *
 * Returns the frame relative to the innermost pushed offset window: the
 * absolute frame minus the sum of all pushed offsets.  With an empty stack
 * this equals the absolute frame.
 *
 * Returns: the relative frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_context_get_frame (LrgReelContext *self);

/**
 * lrg_reel_context_get_absolute_frame:
 * @self: a #LrgReelContext
 *
 * Returns: the absolute (composition-global) frame being rendered
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_context_get_absolute_frame (LrgReelContext *self);

/**
 * lrg_reel_context_set_absolute_frame:
 * @self: a #LrgReelContext
 * @absolute_frame: the new absolute frame
 *
 * Sets the absolute frame and clears the offset stack, so a single context may
 * be reused across an entire render.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_context_set_absolute_frame (LrgReelContext *self,
                                     gint            absolute_frame);

/**
 * lrg_reel_context_get_fps:
 * @self: a #LrgReelContext
 *
 * Returns: the frame rate in frames per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_context_get_fps (LrgReelContext *self);

/**
 * lrg_reel_context_get_seconds:
 * @self: a #LrgReelContext
 *
 * Returns the relative frame converted to seconds (relative frame / fps).
 *
 * Returns: the relative time in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_reel_context_get_seconds (LrgReelContext *self);

/**
 * lrg_reel_context_get_width:
 * @self: a #LrgReelContext
 *
 * Returns: the composition width in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_context_get_width (LrgReelContext *self);

/**
 * lrg_reel_context_get_height:
 * @self: a #LrgReelContext
 *
 * Returns: the composition height in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_context_get_height (LrgReelContext *self);

/**
 * lrg_reel_context_get_duration_in_frames:
 * @self: a #LrgReelContext
 *
 * Returns: the total composition duration in frames
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_reel_context_get_duration_in_frames (LrgReelContext *self);

/**
 * lrg_reel_context_push_offset:
 * @self: a #LrgReelContext
 * @from: the offset (in parent-relative frames) to subtract.
 * @duration_in_frames: the window length, or %LRG_REEL_DURATION_INFINITE.
 *
 * Pushes an offset window.  After this call, lrg_reel_context_get_frame()
 * subtracts @from (cumulatively) and lrg_reel_context_is_active() tests the
 * relative frame against [0, @duration_in_frames).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_context_push_offset (LrgReelContext *self,
                              gint            from,
                              gint            duration_in_frames);

/**
 * lrg_reel_context_pop_offset:
 * @self: a #LrgReelContext
 *
 * Pops the innermost offset window pushed by lrg_reel_context_push_offset().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_context_pop_offset (LrgReelContext *self);

/**
 * lrg_reel_context_is_active:
 * @self: a #LrgReelContext
 *
 * Tests whether the current relative frame falls within the innermost pushed
 * window, i.e. relative >= 0 and (duration is infinite or relative <
 * duration).  With an empty stack this is always %TRUE.
 *
 * Returns: %TRUE if active in the innermost window
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_context_is_active (LrgReelContext *self);

/**
 * lrg_reel_context_test_window:
 * @self: a #LrgReelContext
 * @from: window start in current-relative frames.
 * @duration_in_frames: window length, or %LRG_REEL_DURATION_INFINITE.
 *
 * Tests whether the current relative frame falls within [@from, @from +
 * @duration_in_frames) without pushing anything onto the stack.
 *
 * Returns: %TRUE if the relative frame is inside the window
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_context_test_window (LrgReelContext *self,
                              gint            from,
                              gint            duration_in_frames);

G_END_DECLS
