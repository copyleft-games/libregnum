/* lrg-slide-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Slide transition with push/cover/reveal modes.
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

#define LRG_TYPE_SLIDE_TRANSITION (lrg_slide_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSlideTransition, lrg_slide_transition, LRG, SLIDE_TRANSITION, LrgTransition)

/**
 * lrg_slide_transition_new:
 *
 * Creates a new slide transition with default settings.
 *
 * Returns: (transfer full): A new #LrgSlideTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSlideTransition *    lrg_slide_transition_new                (void);

/**
 * lrg_slide_transition_new_with_options:
 * @direction: The slide direction
 * @mode: The slide mode (push, cover, reveal)
 *
 * Creates a new slide transition with the specified options.
 *
 * Returns: (transfer full): A new #LrgSlideTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSlideTransition *    lrg_slide_transition_new_with_options   (LrgTransitionDirection  direction,
                                                                 LrgSlideMode            mode);

/**
 * lrg_slide_transition_get_direction:
 * @self: A #LrgSlideTransition
 *
 * Gets the slide direction.
 *
 * Returns: The #LrgTransitionDirection
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTransitionDirection  lrg_slide_transition_get_direction      (LrgSlideTransition *self);

/**
 * lrg_slide_transition_set_direction:
 * @self: A #LrgSlideTransition
 * @direction: The slide direction
 *
 * Sets the slide direction.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_slide_transition_set_direction      (LrgSlideTransition     *self,
                                                                 LrgTransitionDirection  direction);

/**
 * lrg_slide_transition_get_mode:
 * @self: A #LrgSlideTransition
 *
 * Gets the slide mode.
 *
 * Returns: The #LrgSlideMode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSlideMode            lrg_slide_transition_get_mode           (LrgSlideTransition *self);

/**
 * lrg_slide_transition_set_mode:
 * @self: A #LrgSlideTransition
 * @mode: The slide mode
 *
 * Sets the slide mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                    lrg_slide_transition_set_mode           (LrgSlideTransition *self,
                                                                 LrgSlideMode        mode);

G_END_DECLS
