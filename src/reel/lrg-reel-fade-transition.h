/* lrg-reel-fade-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelFadeTransition - cross-fade between two reel frames.
 *
 * The outgoing frame is drawn fully onto the canvas, then the incoming frame
 * is composited over it at opacity equal to the eased progress value.  At
 * progress 0.0 only @from is visible; at progress 1.0 only @to is visible.
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

#define LRG_TYPE_REEL_FADE_TRANSITION (lrg_reel_fade_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelFadeTransition, lrg_reel_fade_transition,
                      LRG, REEL_FADE_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_fade_transition_new:
 *
 * Creates a new #LrgReelFadeTransition.
 *
 * Returns: (transfer full): a new #LrgReelFadeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelFadeTransition *
lrg_reel_fade_transition_new (void);

G_END_DECLS
