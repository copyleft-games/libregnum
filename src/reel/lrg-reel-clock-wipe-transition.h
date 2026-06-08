/* lrg-reel-clock-wipe-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelClockWipeTransition - angular sweep transition between two reel frames.
 *
 * The incoming frame (@to) is revealed by an angular sweep that rotates
 * clockwise from the 12 o'clock position.  At progress 0.0 the canvas shows
 * @from entirely; at progress 1.0 the canvas shows @to entirely.
 *
 * The reveal is computed per-pixel: each pixel whose angle from the frame
 * centre (measured clockwise from straight up, normalised to [0, 1)) is less
 * than the current progress value is taken from @to; all others remain from
 * @from.
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

#define LRG_TYPE_REEL_CLOCK_WIPE_TRANSITION (lrg_reel_clock_wipe_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelClockWipeTransition, lrg_reel_clock_wipe_transition,
                      LRG, REEL_CLOCK_WIPE_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_clock_wipe_transition_new:
 *
 * Creates a new #LrgReelClockWipeTransition.
 *
 * Returns: (transfer full): a new #LrgReelClockWipeTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelClockWipeTransition *
lrg_reel_clock_wipe_transition_new (void);

G_END_DECLS
