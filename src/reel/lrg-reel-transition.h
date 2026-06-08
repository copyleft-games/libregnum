/* lrg-reel-transition.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelTransition - derivable base class for CPU reel frame transitions.
 *
 * A transition blends two consecutive frames during programmatic video
 * rendering.  At progress 0.0 the canvas shows @from; at progress 1.0 the
 * canvas shows @to.  The canvas is already sized to the frame (same
 * dimensions as @from and @to).
 *
 * Subclass and override LrgReelTransitionClass.composite to implement a
 * custom effect.  The public lrg_reel_transition_composite() entry-point
 * clamps progress to [0, 1], applies the configured easing curve, then
 * dispatches the vfunc with the eased value.
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

G_BEGIN_DECLS

#define LRG_TYPE_REEL_TRANSITION (lrg_reel_transition_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgReelTransition, lrg_reel_transition,
                          LRG, REEL_TRANSITION, GObject)

/**
 * LrgReelTransitionClass:
 * @parent_class: the parent class.
 * @composite: compose @from and @to onto @canvas at the given eased @progress.
 *   progress 0.0 => canvas shows @from; progress 1.0 => canvas shows @to.
 *   The default implementation does a hard cut: @to is shown when
 *   @progress >= 0.5, otherwise @from.
 *
 * Class structure for #LrgReelTransition.
 *
 * Since: 1.0
 */
struct _LrgReelTransitionClass
{
    GObjectClass parent_class;

    void (*composite) (LrgReelTransition *self,
                       LrgImageCanvas    *canvas,
                       GrlImage          *from,
                       GrlImage          *to,
                       gdouble            progress);

    /*< private >*/
    gpointer _reserved[8];
};

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * lrg_reel_transition_composite:
 * @self: an #LrgReelTransition
 * @canvas: the destination #LrgImageCanvas (already sized to frame dimensions)
 * @from: the outgoing frame #GrlImage
 * @to: the incoming frame #GrlImage
 * @progress: linear transition progress in [0, 1]; values outside are clamped
 *
 * Blends @from and @to onto @canvas according to the transition type.
 *
 * This function clamps @progress to [0, 1], applies the easing function
 * configured on the transition, then calls the composite vfunc with the
 * eased value.
 *
 * Contract: at progress 0.0 the canvas shows @from; at progress 1.0 the
 * canvas shows @to.  @canvas, @from, and @to all share the same dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_transition_composite (LrgReelTransition *self,
                                LrgImageCanvas    *canvas,
                                GrlImage          *from,
                                GrlImage          *to,
                                gdouble            progress);

/**
 * lrg_reel_transition_get_easing:
 * @self: an #LrgReelTransition
 *
 * Returns the easing type applied to the linear progress value before it is
 * passed to the composite vfunc.
 *
 * Returns: the #LrgEasingType
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgEasingType
lrg_reel_transition_get_easing (LrgReelTransition *self);

/**
 * lrg_reel_transition_set_easing:
 * @self: an #LrgReelTransition
 * @easing: the #LrgEasingType to use
 *
 * Sets the easing curve applied to the progress value before compositing.
 * The default is %LRG_EASING_LINEAR.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_transition_set_easing (LrgReelTransition *self,
                                 LrgEasingType      easing);

G_END_DECLS
