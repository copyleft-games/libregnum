/* lrg-fade-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fade transition that fades to/from a color.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-transition.h"

G_BEGIN_DECLS

#define LRG_TYPE_FADE_TRANSITION (lrg_fade_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFadeTransition, lrg_fade_transition, LRG, FADE_TRANSITION, LrgTransition)

/**
 * lrg_fade_transition_new:
 *
 * Creates a new fade transition with default settings (fade to black).
 *
 * Returns: (transfer full): A new #LrgFadeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgFadeTransition * lrg_fade_transition_new             (void);

/**
 * lrg_fade_transition_new_with_color:
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 *
 * Creates a new fade transition that fades to/from the specified color.
 *
 * Returns: (transfer full): A new #LrgFadeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgFadeTransition * lrg_fade_transition_new_with_color  (guint8  r,
                                                         guint8  g,
                                                         guint8  b);

/**
 * lrg_fade_transition_get_color:
 * @self: A #LrgFadeTransition
 * @r: (out) (nullable): Return location for red component
 * @g: (out) (nullable): Return location for green component
 * @b: (out) (nullable): Return location for blue component
 *
 * Gets the fade color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fade_transition_get_color       (LrgFadeTransition *self,
                                                         guint8            *r,
                                                         guint8            *g,
                                                         guint8            *b);

/**
 * lrg_fade_transition_set_color:
 * @self: A #LrgFadeTransition
 * @r: Red component (0-255)
 * @g: Green component (0-255)
 * @b: Blue component (0-255)
 *
 * Sets the fade color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fade_transition_set_color       (LrgFadeTransition *self,
                                                         guint8             r,
                                                         guint8             g,
                                                         guint8             b);

G_END_DECLS
