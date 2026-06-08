/* lrg-reel-zoom-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelZoomTransition - centre-zoom cross-fade between two reel frames.
 *
 * The incoming frame (@to) zooms in from a small initial scale up to full
 * size while simultaneously fading in over @from.  At progress 0.0 @to is
 * scaled to 30 % of the frame dimensions with zero opacity; at progress 1.0
 * @to is at full size with full opacity, completely replacing @from.
 *
 * The zoom is centred: the destination rectangle is computed so that the
 * scaled @to image remains centred in the frame at every progress value.
 * @from is always drawn at full size as the background layer.
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

#define LRG_TYPE_REEL_ZOOM_TRANSITION (lrg_reel_zoom_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelZoomTransition, lrg_reel_zoom_transition,
                      LRG, REEL_ZOOM_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_zoom_transition_new:
 *
 * Creates a new #LrgReelZoomTransition.
 *
 * Returns: (transfer full): a new #LrgReelZoomTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelZoomTransition *
lrg_reel_zoom_transition_new (void);

G_END_DECLS
