/* lrg-component.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for game object components.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-component.h"
#include "lrg-component-private.h"
#include "lrg-game-object.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    LrgGameObject *owner;    /* Weak reference to owning game object */
    gboolean       enabled;  /* Whether component receives updates */
} LrgComponentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgComponent, lrg_component, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_OWNER,
    PROP_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Internal Functions (used by LrgGameObject)
 * ========================================================================== */

/*
 * _lrg_component_set_owner:
 * @self: an #LrgComponent
 * @owner: (nullable): the new owner
 *
 * Sets the component's owner. This is called by LrgGameObject when
 * adding/removing components. Do not call directly.
 *
 * If @owner is non-NULL, calls the attached() virtual method.
 * If @owner is NULL (detaching), calls the detached() virtual method first.
 */
void
_lrg_component_set_owner (LrgComponent  *self,
                          LrgGameObject *owner)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);
    klass = LRG_COMPONENT_GET_CLASS (self);

    /* If detaching (owner becoming NULL), call detached first */
    if (owner == NULL && priv->owner != NULL)
    {
        if (klass->detached != NULL)
        {
            klass->detached (self);
        }
    }

    /* Update the owner reference */
    priv->owner = owner;

    /* If attaching (new owner), call attached */
    if (owner != NULL)
    {
        if (klass->attached != NULL)
        {
            klass->attached (self, owner);
        }
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_OWNER]);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_component_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
    LrgComponent        *self = LRG_COMPONENT (object);
    LrgComponentPrivate *priv = lrg_component_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_OWNER:
        g_value_set_object (value, priv->owner);
        break;
    case PROP_ENABLED:
        g_value_set_boolean (value, priv->enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_component_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
    LrgComponent *self = LRG_COMPONENT (object);

    switch (prop_id)
    {
    case PROP_ENABLED:
        lrg_component_set_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_component_class_init (LrgComponentClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_component_get_property;
    object_class->set_property = lrg_component_set_property;

    /* No finalize needed - owner is weak reference, no allocations */

    /* Default virtual method implementations (NULL = no-op) */
    klass->attached = NULL;
    klass->detached = NULL;
    klass->update = NULL;

    /**
     * LrgComponent:owner:
     *
     * The game object that owns this component.
     *
     * This property is read-only and is set automatically when the
     * component is added to or removed from a game object.
     */
    properties[PROP_OWNER] =
        g_param_spec_object ("owner",
                             "Owner",
                             "The game object that owns this component",
                             LRG_TYPE_GAME_OBJECT,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgComponent:enabled:
     *
     * Whether the component is enabled.
     *
     * Disabled components do not receive update() calls.
     */
    properties[PROP_ENABLED] =
        g_param_spec_boolean ("enabled",
                              "Enabled",
                              "Whether the component receives updates",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_component_init (LrgComponent *self)
{
    LrgComponentPrivate *priv = lrg_component_get_instance_private (self);

    priv->owner = NULL;
    priv->enabled = TRUE;
}

/* ==========================================================================
 * Public API - Property Accessors
 * ========================================================================== */

/**
 * lrg_component_get_owner:
 * @self: an #LrgComponent
 *
 * Gets the game object that owns this component.
 *
 * Returns: (transfer none) (nullable): The owning #LrgGameObject, or %NULL
 */
LrgGameObject *
lrg_component_get_owner (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), NULL);

    priv = lrg_component_get_instance_private (self);
    return priv->owner;
}

/**
 * lrg_component_get_enabled:
 * @self: an #LrgComponent
 *
 * Gets whether this component is enabled.
 *
 * Returns: %TRUE if the component is enabled
 */
gboolean
lrg_component_get_enabled (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), FALSE);

    priv = lrg_component_get_instance_private (self);
    return priv->enabled;
}

/**
 * lrg_component_set_enabled:
 * @self: an #LrgComponent
 * @enabled: whether to enable the component
 *
 * Sets whether this component is enabled.
 */
void
lrg_component_set_enabled (LrgComponent *self,
                           gboolean      enabled)
{
    LrgComponentPrivate *priv;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);

    enabled = !!enabled;  /* Normalize to 0/1 */

    if (priv->enabled != enabled)
    {
        priv->enabled = enabled;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
    }
}

/* ==========================================================================
 * Public API - Methods
 * ========================================================================== */

/**
 * lrg_component_update:
 * @self: an #LrgComponent
 * @delta: time elapsed since last frame in seconds
 *
 * Updates the component for the current frame.
 *
 * This calls the virtual update() method if the component is enabled.
 */
void
lrg_component_update (LrgComponent *self,
                      gfloat        delta)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);

    /* Only update if enabled */
    if (!priv->enabled)
    {
        return;
    }

    klass = LRG_COMPONENT_GET_CLASS (self);
    if (klass->update != NULL)
    {
        klass->update (self, delta);
    }
}
