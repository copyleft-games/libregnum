/* lrg-wipe-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Directional wipe transition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_WIPE_TRANSITION (lrg_wipe_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWipeTransition, lrg_wipe_transition, LRG, WIPE_TRANSITION, LrgTransition)

/**
 * lrg_wipe_transition_new:
 *
 * Creates a new wipe transition with default settings (wipe from left).
 *
 * Returns: (transfer full): A new #LrgWipeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWipeTransition * lrg_wipe_transition_new                 (void);

/**
 * lrg_wipe_transition_new_with_direction:
 * @direction: The wipe direction
 *
 * Creates a new wipe transition with the specified direction.
 *
 * Returns: (transfer full): A new #LrgWipeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgWipeTransition * lrg_wipe_transition_new_with_direction  (LrgTransitionDirection  direction);

/**
 * lrg_wipe_transition_get_direction:
 * @self: A #LrgWipeTransition
 *
 * Gets the wipe direction.
 *
 * Returns: The #LrgTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionDirection  lrg_wipe_transition_get_direction   (LrgWipeTransition *self);

/**
 * lrg_wipe_transition_set_direction:
 * @self: A #LrgWipeTransition
 * @direction: The wipe direction
 *
 * Sets the wipe direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_wipe_transition_set_direction       (LrgWipeTransition      *self,
                                                             LrgTransitionDirection  direction);

/**
 * lrg_wipe_transition_get_softness:
 * @self: A #LrgWipeTransition
 *
 * Gets the edge softness (blur amount at wipe edge).
 *
 * Returns: Softness value (0.0 = hard edge, 1.0 = very soft)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_wipe_transition_get_softness        (LrgWipeTransition *self);

/**
 * lrg_wipe_transition_set_softness:
 * @self: A #LrgWipeTransition
 * @softness: Edge softness (0.0 = hard, 1.0 = soft)
 *
 * Sets the edge softness.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_wipe_transition_set_softness        (LrgWipeTransition *self,
                                                             gfloat             softness);

G_END_DECLS
