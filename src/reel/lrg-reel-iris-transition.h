/* lrg-reel-iris-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelIrisTransition - circular iris reveal between two reel frames.
 *
 * The incoming frame (@to) is revealed through a circle that grows from the
 * centre of the frame outward as progress advances from 0 to 1.  When
 * #LrgReelIrisTransition:inverse is %TRUE the effect is reversed: @to is
 * shown first and a circle of @from closes inward until @to is fully hidden.
 *
 * The maximum radius equals half the diagonal of the frame so that the circle
 * completely covers all four corners at progress 1.0.
 *
 * The reveal is computed per-pixel: pixels whose distance from the frame
 * centre is less than (progress × max_radius) are taken from @to; all others
 * remain from @from.
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

#define LRG_TYPE_REEL_IRIS_TRANSITION (lrg_reel_iris_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelIrisTransition, lrg_reel_iris_transition,
                      LRG, REEL_IRIS_TRANSITION, LrgReelTransition)

/**
 * lrg_reel_iris_transition_new:
 *
 * Creates a new #LrgReelIrisTransition with the default settings
 * (#LrgReelIrisTransition:inverse = %FALSE).
 *
 * Returns: (transfer full): a new #LrgReelIrisTransition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelIrisTransition *
lrg_reel_iris_transition_new (void);

/**
 * lrg_reel_iris_transition_get_inverse:
 * @self: an #LrgReelIrisTransition
 *
 * Returns whether the iris direction is inverted.
 *
 * Returns: %TRUE if the iris closes instead of opens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_iris_transition_get_inverse (LrgReelIrisTransition *self);

/**
 * lrg_reel_iris_transition_set_inverse:
 * @self: an #LrgReelIrisTransition
 * @inverse: %TRUE to invert the iris direction
 *
 * When @inverse is %FALSE (the default) @to is revealed by a growing circle.
 * When %TRUE @from is revealed by a growing circle (i.e. @to closes away).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_iris_transition_set_inverse (LrgReelIrisTransition *self,
                                       gboolean               inverse);

G_END_DECLS
