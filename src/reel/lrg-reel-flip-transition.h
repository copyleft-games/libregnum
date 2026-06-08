/* lrg-reel-flip-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelFlipTransition - card-flip transition between two reel frames.
 *
 * Simulates a flat card being flipped along its central axis.  For a
 * horizontal (LEFT / RIGHT) flip the card shrinks on the X axis in the first
 * half of the transition (revealing the back of @from), then grows back on
 * the X axis in the second half (revealing @to).  UP / DOWN produces the
 * same effect on the Y axis.
 *
 * The #LrgReelFlipTransition:direction property controls the axis of rotation:
 *   - LEFT / RIGHT: horizontal flip (card rotates around its vertical centre).
 *   - UP   / DOWN:  vertical flip   (card rotates around its horizontal centre).
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

#define LRG_TYPE_REEL_FLIP_TRANSITION (lrg_reel_flip_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelFlipTransition, lrg_reel_flip_transition,
                      LRG, REEL_FLIP_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_flip_transition_new:
 * @direction: axis of the flip; LEFT/RIGHT for horizontal, UP/DOWN for vertical
 *
 * Creates a new #LrgReelFlipTransition.
 *
 * Returns: (transfer full): a new #LrgReelFlipTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelFlipTransition *
lrg_reel_flip_transition_new (LrgReelTransitionDirection direction);

/**
 * lrg_reel_flip_transition_get_direction:
 * @self: an #LrgReelFlipTransition
 *
 * Returns the flip axis direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTransitionDirection
lrg_reel_flip_transition_get_direction (LrgReelFlipTransition *self);

/**
 * lrg_reel_flip_transition_set_direction:
 * @self: an #LrgReelFlipTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the flip axis direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_flip_transition_set_direction (LrgReelFlipTransition      *self,
                                         LrgReelTransitionDirection  direction);

G_END_DECLS
