/* lrg-arrangement-carousel.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The "carousel" arrangement: panels on a ring that curves around the viewer.
 * The panel whose slot ordinal is 0 sits at the front (the origin, facing the
 * default camera); lower ordinals run off to the left, higher ones to the
 * right.  A host sets ordinal = index - active index, so switching the active
 * window spins the ring.  A change of slot group leaves an extra gap, so tab
 * groups read as clusters.  With many panels the ring grows until it closes
 * into a full circle.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_ARRANGEMENT_CAROUSEL (lrg_arrangement_carousel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgArrangementCarousel, lrg_arrangement_carousel, LRG, ARRANGEMENT_CAROUSEL, GObject)

LRG_AVAILABLE_IN_ALL
LrgArrangementCarousel * lrg_arrangement_carousel_new (void);

/**
 * lrg_arrangement_carousel_set_radius:
 * @self: a #LrgArrangementCarousel
 * @radius: the minimum ring radius in world units (clamped to [2, 100])
 *
 * The ring grows past this when the panels would not fit on a full circle.
 * Defaults to 7.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_arrangement_carousel_set_radius (LrgArrangementCarousel *self,
                                          gfloat                  radius);

LRG_AVAILABLE_IN_ALL
gfloat lrg_arrangement_carousel_get_radius (LrgArrangementCarousel *self);

/**
 * lrg_arrangement_carousel_set_spacing:
 * @self: a #LrgArrangementCarousel
 * @spacing: centre-to-centre distance as a multiple of the panel width
 *   (clamped to [1, 4]); defaults to 1.15
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_arrangement_carousel_set_spacing (LrgArrangementCarousel *self,
                                           gfloat                  spacing);

LRG_AVAILABLE_IN_ALL
gfloat lrg_arrangement_carousel_get_spacing (LrgArrangementCarousel *self);

G_END_DECLS
