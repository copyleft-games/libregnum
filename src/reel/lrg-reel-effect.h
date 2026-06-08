/* lrg-reel-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelEffect - a post-draw image filter attached to a clip.
 *
 * Effects are applied, in order, to a clip's composited off-screen layer before
 * it is blended onto the frame (so an effect sees exactly the clip's pixels).
 * Subclass and override the @apply vfunc to mutate the RGBA8 image in place.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_EFFECT (lrg_reel_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgReelEffect, lrg_reel_effect, LRG, REEL_EFFECT, GObject)

/**
 * LrgReelEffectClass:
 * @parent_class: the parent class.
 * @apply: mutate @image (an RGBA8 #GrlImage) in place for the given context.
 *
 * Class structure for #LrgReelEffect.
 *
 * Since: 1.0
 */
struct _LrgReelEffectClass
{
    GObjectClass parent_class;

    void (*apply) (LrgReelEffect  *self,
                   GrlImage       *image,
                   LrgReelContext *ctx);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * lrg_reel_effect_apply:
 * @self: a #LrgReelEffect
 * @image: the RGBA8 #GrlImage to filter in place.
 * @ctx: the #LrgReelContext for the current frame.
 *
 * Applies the effect to @image.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_effect_apply (LrgReelEffect  *self,
                       GrlImage       *image,
                       LrgReelContext *ctx);

/**
 * lrg_reel_effect_get_enabled:
 * @self: a #LrgReelEffect
 *
 * Returns: whether the effect is enabled (default %TRUE)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_effect_get_enabled (LrgReelEffect *self);

LRG_AVAILABLE_IN_ALL
void
lrg_reel_effect_set_enabled (LrgReelEffect *self,
                             gboolean       enabled);

G_END_DECLS
