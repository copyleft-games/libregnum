/* lrg-reel-slide-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelSlideTransition - incoming frame slides in over the outgoing frame.
 *
 * The @from frame remains stationary while @to slides in from the edge
 * indicated by #LrgReelSlideTransition:direction.  At progress 0.0 @to is
 * completely off-screen; at progress 1.0 @to covers the canvas entirely.
 *
 * Direction semantics (the edge @to enters FROM):
 *   - LEFT:  @to enters from the left, sliding rightward into place.
 *   - RIGHT: @to enters from the right, sliding leftward into place.
 *   - UP:    @to enters from the top, sliding downward into place.
 *   - DOWN:  @to enters from the bottom, sliding upward into place.
 *
 * Off-canvas parts of @to are clipped naturally by the underlying image
 * rasterizer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-reel-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_SLIDE_TRANSITION (lrg_reel_slide_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelSlideTransition, lrg_reel_slide_transition,
                      LRG, REEL_SLIDE_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_slide_transition_new:
 * @direction: the edge from which the incoming frame slides in
 *
 * Creates a new #LrgReelSlideTransition.
 *
 * Returns: (transfer full): a new #LrgReelSlideTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelSlideTransition *
lrg_reel_slide_transition_new (LrgReelTransitionDirection direction);

/**
 * lrg_reel_slide_transition_get_direction:
 * @self: an #LrgReelSlideTransition
 *
 * Returns the slide direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTransitionDirection
lrg_reel_slide_transition_get_direction (LrgReelSlideTransition *self);

/**
 * lrg_reel_slide_transition_set_direction:
 * @self: an #LrgReelSlideTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the slide direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_slide_transition_set_direction (LrgReelSlideTransition     *self,
                                          LrgReelTransitionDirection  direction);

G_END_DECLS
