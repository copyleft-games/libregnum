/* lrg-reel-dissolve-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelDissolveTransition - deterministic per-pixel dissolve between frames.
 *
 * Each pixel is revealed from @from to @to at a threshold derived from a
 * deterministic hash of the pixel coordinates and the seed value.  The reveal
 * order is pseudo-random but perfectly reproducible for a given seed, making
 * it suitable for offline frame baking.
 *
 * The algorithm is O(width * height) per composite call.
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

#define LRG_TYPE_REEL_DISSOLVE_TRANSITION (lrg_reel_dissolve_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelDissolveTransition, lrg_reel_dissolve_transition,
                      LRG, REEL_DISSOLVE_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_dissolve_transition_new:
 *
 * Creates a new #LrgReelDissolveTransition with the default seed (1337).
 *
 * Returns: (transfer full): a new #LrgReelDissolveTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelDissolveTransition *
lrg_reel_dissolve_transition_new (void);

/**
 * lrg_reel_dissolve_transition_get_seed:
 * @self: an #LrgReelDissolveTransition
 *
 * Returns the hash seed used to determine per-pixel reveal order.
 *
 * Returns: the seed value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_reel_dissolve_transition_get_seed (LrgReelDissolveTransition *self);

/**
 * lrg_reel_dissolve_transition_set_seed:
 * @self: an #LrgReelDissolveTransition
 * @seed: the new seed value
 *
 * Sets the hash seed.  Changing the seed produces a different but equally
 * reproducible dissolve pattern.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_dissolve_transition_set_seed (LrgReelDissolveTransition *self,
                                        guint                      seed);

G_END_DECLS
