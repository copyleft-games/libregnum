/* lrg-rating.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgRating: A row of stars (or other shapes) for a 0..N rating, with halves.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

#define LRG_TYPE_RATING (lrg_rating_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRating, lrg_rating, LRG, RATING, LrgWidget)

/**
 * lrg_rating_new:
 *
 * Returns: (transfer full): a new #LrgRating
 */
LRG_AVAILABLE_IN_ALL
LrgRating * lrg_rating_new (void);

G_END_DECLS
