/* lrg-reel-transition.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelTransition - derivable base class for CPU reel frame transitions.
 */

#include "config.h"
#include "lrg-reel-transition.h"
#include "../graphics/lrg-image-canvas.h"
#include "../tween/lrg-easing.h"

typedef struct
{
    LrgEasingType easing;
} LrgReelTransitionPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgReelTransition, lrg_reel_transition, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_EASING,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* --------------------------------------------------------------------------
 * Default composite vfunc: hard cut at progress == 0.5
 * -------------------------------------------------------------------------- */

static void
lrg_reel_transition_real_composite (LrgReelTransition *self,
                                     LrgImageCanvas    *canvas,
                                     GrlImage          *from,
                                     GrlImage          *to,
                                     gdouble            progress)
{
    GrlImage *src;

    src = (progress >= 0.5) ? to : from;
    grl_image_draw_image (lrg_image_canvas_get_image (canvas),
                          src, NULL, NULL, NULL);
}

/* --------------------------------------------------------------------------
 * GObject boilerplate
 * -------------------------------------------------------------------------- */

static void
lrg_reel_transition_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgReelTransition        *self = LRG_REEL_TRANSITION (object);
    LrgReelTransitionPrivate *priv;

    priv = lrg_reel_transition_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_EASING:
        g_value_set_enum (value, (gint) priv->easing);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_transition_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
    LrgReelTransition        *self = LRG_REEL_TRANSITION (object);
    LrgReelTransitionPrivate *priv;

    priv = lrg_reel_transition_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_EASING:
        priv->easing = (LrgEasingType) g_value_get_enum (value);
        g_object_notify_by_pspec (object, pspec);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reel_transition_class_init (LrgReelTransitionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_reel_transition_get_property;
    object_class->set_property = lrg_reel_transition_set_property;

    klass->composite = lrg_reel_transition_real_composite;

    /**
     * LrgReelTransition:easing:
     *
     * Easing function applied to the linear progress value before it is
     * forwarded to the composite vfunc.  The default is %LRG_EASING_LINEAR.
     *
     * Since: 1.0
     */
    properties[PROP_EASING] =
        g_param_spec_enum ("easing",
                           "Easing",
                           "Easing function applied to transition progress",
                           LRG_TYPE_EASING_TYPE,
                           LRG_EASING_LINEAR,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reel_transition_init (LrgReelTransition *self)
{
    LrgReelTransitionPrivate *priv;

    priv = lrg_reel_transition_get_instance_private (self);
    priv->easing = LRG_EASING_LINEAR;
}

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
 * Blends @from and @to onto @canvas.  @progress is clamped to [0, 1],
 * eased according to the #LrgReelTransition:easing property, then passed to
 * the composite vfunc.
 *
 * Since: 1.0
 */
void
lrg_reel_transition_composite (LrgReelTransition *self,
                                LrgImageCanvas    *canvas,
                                GrlImage          *from,
                                GrlImage          *to,
                                gdouble            progress)
{
    LrgReelTransitionPrivate *priv;
    gdouble                   clamped;
    gfloat                    eased;

    g_return_if_fail (LRG_IS_REEL_TRANSITION (self));
    g_return_if_fail (LRG_IS_IMAGE_CANVAS (canvas));
    g_return_if_fail (GRL_IS_IMAGE (from));
    g_return_if_fail (GRL_IS_IMAGE (to));

    priv = lrg_reel_transition_get_instance_private (self);

    clamped = CLAMP (progress, 0.0, 1.0);
    eased   = lrg_easing_apply (priv->easing, (gfloat) clamped);

    LRG_REEL_TRANSITION_GET_CLASS (self)->composite (self, canvas,
                                                      from, to,
                                                      (gdouble) eased);
}

/**
 * lrg_reel_transition_get_easing:
 * @self: an #LrgReelTransition
 *
 * Returns the easing type.
 *
 * Returns: the #LrgEasingType
 *
 * Since: 1.0
 */
LrgEasingType
lrg_reel_transition_get_easing (LrgReelTransition *self)
{
    LrgReelTransitionPrivate *priv;

    g_return_val_if_fail (LRG_IS_REEL_TRANSITION (self), LRG_EASING_LINEAR);

    priv = lrg_reel_transition_get_instance_private (self);
    return priv->easing;
}

/**
 * lrg_reel_transition_set_easing:
 * @self: an #LrgReelTransition
 * @easing: the #LrgEasingType to use
 *
 * Sets the easing curve applied to the progress value.
 *
 * Since: 1.0
 */
void
lrg_reel_transition_set_easing (LrgReelTransition *self,
                                 LrgEasingType      easing)
{
    LrgReelTransitionPrivate *priv;

    g_return_if_fail (LRG_IS_REEL_TRANSITION (self));

    priv = lrg_reel_transition_get_instance_private (self);

    if (priv->easing == easing)
        return;

    priv->easing = easing;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EASING]);
}
