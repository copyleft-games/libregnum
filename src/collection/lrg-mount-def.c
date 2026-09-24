/* lrg-mount-def.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"

#include <math.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "collection/lrg-mount-def.h"

typedef struct
{
    gdouble  speed_multiplier;
    guint    required_riding;
    gboolean flying;
    guint    passengers;
} LrgMountDefPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgMountDef, lrg_mount_def, LRG_TYPE_COLLECTIBLE_DEF)

enum
{
    PROP_0,
    PROP_SPEED_MULTIPLIER,
    PROP_REQUIRED_RIDING,
    PROP_FLYING,
    PROP_PASSENGERS,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_mount_def_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgMountDef        *self = LRG_MOUNT_DEF (object);
    LrgMountDefPrivate *priv = lrg_mount_def_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_SPEED_MULTIPLIER:
        g_value_set_double (value, priv->speed_multiplier);
        break;
    case PROP_REQUIRED_RIDING:
        g_value_set_uint (value, priv->required_riding);
        break;
    case PROP_FLYING:
        g_value_set_boolean (value, priv->flying);
        break;
    case PROP_PASSENGERS:
        g_value_set_uint (value, priv->passengers);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_mount_def_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgMountDef *self = LRG_MOUNT_DEF (object);

    switch (prop_id)
    {
    case PROP_SPEED_MULTIPLIER:
        lrg_mount_def_set_speed_multiplier (self, g_value_get_double (value));
        break;
    case PROP_REQUIRED_RIDING:
        lrg_mount_def_set_required_riding (self, g_value_get_uint (value));
        break;
    case PROP_FLYING:
        lrg_mount_def_set_flying (self, g_value_get_boolean (value));
        break;
    case PROP_PASSENGERS:
        lrg_mount_def_set_passengers (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_mount_def_class_init (LrgMountDefClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_mount_def_get_property;
    object_class->set_property = lrg_mount_def_set_property;

    /**
     * LrgMountDef:speed-multiplier:
     *
     * The mount's own speed multiplier, applied on top of riding skill.
     */
    properties[PROP_SPEED_MULTIPLIER] =
        g_param_spec_double ("speed-multiplier", "Speed Multiplier",
                             "Mount speed multiplier",
                             1.0, LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER, 1.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgMountDef:required-riding:
     *
     * Minimum riding skill tier.
     */
    properties[PROP_REQUIRED_RIDING] =
        g_param_spec_uint ("required-riding", "Required Riding",
                           "Minimum riding skill tier",
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgMountDef:flying:
     *
     * Whether the mount can fly.
     */
    properties[PROP_FLYING] =
        g_param_spec_boolean ("flying", "Flying", "Whether the mount can fly",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                              G_PARAM_STATIC_STRINGS);

    /**
     * LrgMountDef:passengers:
     *
     * Extra passenger seats.
     */
    properties[PROP_PASSENGERS] =
        g_param_spec_uint ("passengers", "Passengers", "Extra passenger seats",
                           0, LRG_MOUNT_DEF_MAX_PASSENGERS, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_mount_def_init (LrgMountDef *self)
{
    LrgMountDefPrivate *priv = lrg_mount_def_get_instance_private (self);

    priv->speed_multiplier = 1.0;
    priv->required_riding = 1;
    priv->flying = FALSE;
    priv->passengers = 0;
}

LrgMountDef *
lrg_mount_def_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL && *id != '\0', NULL);

    return g_object_new (LRG_TYPE_MOUNT_DEF,
                         "id", id,
                         "kind", LRG_COLLECTIBLE_KIND_MOUNT,
                         NULL);
}

gdouble
lrg_mount_def_get_speed_multiplier (LrgMountDef *self)
{
    LrgMountDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), 1.0);
    priv = lrg_mount_def_get_instance_private (self);
    return priv->speed_multiplier;
}

void
lrg_mount_def_set_speed_multiplier (LrgMountDef *self,
                                    gdouble      multiplier)
{
    LrgMountDefPrivate *priv;

    g_return_if_fail (LRG_IS_MOUNT_DEF (self));
    g_return_if_fail (isfinite (multiplier));
    g_return_if_fail (multiplier >= 1.0 &&
                      multiplier <= LRG_MOUNT_DEF_MAX_SPEED_MULTIPLIER);
    priv = lrg_mount_def_get_instance_private (self);

    if (priv->speed_multiplier == multiplier)
        return;

    priv->speed_multiplier = multiplier;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPEED_MULTIPLIER]);
}

guint
lrg_mount_def_get_required_riding (LrgMountDef *self)
{
    LrgMountDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), 0);
    priv = lrg_mount_def_get_instance_private (self);
    return priv->required_riding;
}

void
lrg_mount_def_set_required_riding (LrgMountDef *self,
                                   guint        tier)
{
    LrgMountDefPrivate *priv;

    g_return_if_fail (LRG_IS_MOUNT_DEF (self));
    priv = lrg_mount_def_get_instance_private (self);

    if (priv->required_riding == tier)
        return;

    priv->required_riding = tier;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_REQUIRED_RIDING]);
}

gboolean
lrg_mount_def_get_flying (LrgMountDef *self)
{
    LrgMountDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), FALSE);
    priv = lrg_mount_def_get_instance_private (self);
    return priv->flying;
}

void
lrg_mount_def_set_flying (LrgMountDef *self,
                          gboolean     flying)
{
    LrgMountDefPrivate *priv;

    g_return_if_fail (LRG_IS_MOUNT_DEF (self));
    priv = lrg_mount_def_get_instance_private (self);

    flying = !!flying;
    if (priv->flying == flying)
        return;

    priv->flying = flying;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FLYING]);
}

guint
lrg_mount_def_get_passengers (LrgMountDef *self)
{
    LrgMountDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), 0);
    priv = lrg_mount_def_get_instance_private (self);
    return priv->passengers;
}

void
lrg_mount_def_set_passengers (LrgMountDef *self,
                              guint        passengers)
{
    LrgMountDefPrivate *priv;

    g_return_if_fail (LRG_IS_MOUNT_DEF (self));
    g_return_if_fail (passengers <= LRG_MOUNT_DEF_MAX_PASSENGERS);
    priv = lrg_mount_def_get_instance_private (self);

    if (priv->passengers == passengers)
        return;

    priv->passengers = passengers;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PASSENGERS]);
}

gboolean
lrg_mount_def_can_ride (LrgMountDef *self,
                        guint        riding_tier,
                        guint        level)
{
    LrgMountDefPrivate *priv;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), FALSE);
    priv = lrg_mount_def_get_instance_private (self);

    /* Both the riding skill tier and the character level gate riding. */
    if (riding_tier < priv->required_riding)
        return FALSE;
    if (level < lrg_collectible_def_get_required_level (LRG_COLLECTIBLE_DEF (self)))
        return FALSE;

    return TRUE;
}

gdouble
lrg_mount_def_get_speed (LrgMountDef *self,
                         gdouble      base_speed,
                         gdouble      riding_multiplier)
{
    LrgMountDefPrivate *priv;
    gdouble             speed;

    g_return_val_if_fail (LRG_IS_MOUNT_DEF (self), 0.0);
    priv = lrg_mount_def_get_instance_private (self);

    if (!isfinite (base_speed) || !isfinite (riding_multiplier) ||
        base_speed < 0.0 || riding_multiplier < 0.0)
        return 0.0;

    speed = base_speed * riding_multiplier * priv->speed_multiplier;
    if (!isfinite (speed))
        return 0.0;

    return speed;
}
