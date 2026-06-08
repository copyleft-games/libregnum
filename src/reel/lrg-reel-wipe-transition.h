/* lrg-reel-wipe-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelWipeTransition - hard-edge wipe between two reel frames.
 *
 * A rectangular reveal grows from one edge of the frame, progressively
 * exposing @to over @from.  At progress 0.0 the canvas shows @from
 * entirely; at progress 1.0 the canvas shows @to entirely.
 *
 * The #LrgReelWipeTransition:direction property controls which edge the
 * reveal grows from:
 *   - RIGHT: reveal grows from the left edge toward the right.
 *   - LEFT:  reveal grows from the right edge toward the left.
 *   - DOWN:  reveal grows from the top edge downward.
 *   - UP:    reveal grows from the bottom edge upward.
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

#define LRG_TYPE_REEL_WIPE_TRANSITION (lrg_reel_wipe_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelWipeTransition, lrg_reel_wipe_transition,
                      LRG, REEL_WIPE_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_wipe_transition_new:
 * @direction: the edge from which the incoming frame is revealed
 *
 * Creates a new #LrgReelWipeTransition.
 *
 * Returns: (transfer full): a new #LrgReelWipeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelWipeTransition *
lrg_reel_wipe_transition_new (LrgReelTransitionDirection direction);

/**
 * lrg_reel_wipe_transition_get_direction:
 * @self: an #LrgReelWipeTransition
 *
 * Returns the wipe direction.
 *
 * Returns: the #LrgReelTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelTransitionDirection
lrg_reel_wipe_transition_get_direction (LrgReelWipeTransition *self);

/**
 * lrg_reel_wipe_transition_set_direction:
 * @self: an #LrgReelWipeTransition
 * @direction: the new #LrgReelTransitionDirection
 *
 * Sets the wipe direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_wipe_transition_set_direction (LrgReelWipeTransition      *self,
                                         LrgReelTransitionDirection  direction);

G_END_DECLS
