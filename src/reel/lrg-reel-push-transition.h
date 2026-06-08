/* lrg-reel-push-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelPushTransition - simultaneous push-out/push-in between two frames.
 *
 * Both frames move together: @from slides out of the canvas while @to slides
 * in from the opposite edge, maintaining a seamless contact line between them
 * throughout the transition.
 *
 * The #LrgReelPushTransition:direction property controls the travel direction
 * of the outgoing frame (@from):
 *   - LEFT:  @from exits left; @to enters from the right.
 *   - RIGHT: @from exits right; @to enters from the left.
 *   - UP:    @from exits upward; @to enters from the bottom.
 *   - DOWN:  @from exits downward; @to enters from the top.
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

#define LRG_TYPE_REEL_PUSH_TRANSITION (lrg_reel_push_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelPushTransition, lrg_reel_push_transition,
                      LRG, REEL_PUSH_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_push_transition_new:
 * @direction: the direction @from is pushed out of the frame
 *
 * Creates a new #LrgReelPushTransition.
 *
 * Returns: (transfer full): a new #LrgReelPushTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelPushTransition *
lrg_reel_push_transition_new (LrgReelTransitionDirection direction);

/**
 * lrg_reel_push_transition_get_direction:
 * @self: an #LrgReelPushTransition
 *
 * Returns the push direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTransitionDirection
lrg_reel_push_transition_get_direction (LrgReelPushTransition *self);

/**
 * lrg_reel_push_transition_set_direction:
 * @self: an #LrgReelPushTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the push direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_push_transition_set_direction (LrgReelPushTransition      *self,
                                         LrgReelTransitionDirection  direction);

G_END_DECLS
