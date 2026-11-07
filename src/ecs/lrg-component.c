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
#include "../world3d/lrg-entity3d.h"
#include "../world3d/lrg-world3d.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    LrgGameObject *owner;    /* Weak reference to owning game object */
    LrgEntity3D   *entity;   /* Weak reference to owning 3D entity (0.9.0);
                              * at most one of owner/entity is set */
    gboolean       enabled;  /* Whether component receives updates */
} LrgComponentPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgComponent, lrg_component, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_OWNER,
    PROP_ENABLED,
    PROP_ENTITY,
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

    /* A component belongs to one owner kind at a time. */
    if (owner != NULL && priv->entity != NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Component %s is attached to an LrgEntity3D; "
                     "remove it there before adding it to a game object",
                     G_OBJECT_TYPE_NAME (self));
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

/*
 * _lrg_component_set_entity:
 * @self: an #LrgComponent
 * @entity: (nullable): the new owning entity
 *
 * Internal; called only by lrg-entity3d.c (which declares the prototype
 * itself, so no private header is needed).  Stores the weak owner link and
 * runs the attached_entity / detached_entity hooks: detached_entity runs
 * while the old link is still set, attached_entity after the new one is.
 */
void _lrg_component_set_entity (LrgComponent *self,
                                LrgEntity3D  *entity);

void
_lrg_component_set_entity (LrgComponent *self,
                           LrgEntity3D  *entity)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;
    LrgEntity3D         *old;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);
    klass = LRG_COMPONENT_GET_CLASS (self);
    old = priv->entity;

    if (old == entity)
        return;

    /* Leaving the old entity: hook first, then drop the link. */
    if (old != NULL && klass->detached_entity != NULL)
        klass->detached_entity (self, old);

    priv->entity = entity;

    /* Joining the new entity: link first, then the hook. */
    if (entity != NULL && klass->attached_entity != NULL)
        klass->attached_entity (self, entity);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENTITY]);
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
    case PROP_ENTITY:
        g_value_set_object (value, priv->entity);
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

    /**
     * LrgComponent:entity:
     *
     * The #LrgEntity3D that owns this component (0.9.0), or %NULL when
     * the component is unowned or owned by an #LrgGameObject.  Read-only;
     * set by lrg_entity3d_add_component() / lrg_entity3d_remove_component().
     */
    properties[PROP_ENTITY] =
        g_param_spec_object ("entity",
                             "Entity",
                             "The 3D entity that owns this component",
                             LRG_TYPE_ENTITY3D,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_component_init (LrgComponent *self)
{
    LrgComponentPrivate *priv = lrg_component_get_instance_private (self);

    priv->owner = NULL;
    priv->entity = NULL;
    priv->enabled = TRUE;
}

/* ==========================================================================
 * Public API - Property Accessors
 * ========================================================================== */

/*
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
    g_return_val_if_fail (LRG_IS_COMPONENT (self), NULL);

    return _lrg_component_get_owner_unchecked (self);
}

/* See lrg-component-private.h. */
LrgGameObject *
_lrg_component_get_owner_unchecked (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    priv = lrg_component_get_instance_private (self);
    return priv->owner;
}

/*
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
    g_return_val_if_fail (LRG_IS_COMPONENT (self), FALSE);

    return _lrg_component_get_enabled_unchecked (self);
}

/* See lrg-component-private.h. */
gboolean
_lrg_component_get_enabled_unchecked (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    priv = lrg_component_get_instance_private (self);
    return priv->enabled;
}

/*
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

/*
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
    g_return_if_fail (LRG_IS_COMPONENT (self));

    _lrg_component_update_unchecked (self, delta);
}

/* See lrg-component-private.h. */
void
_lrg_component_update_unchecked (LrgComponent *self,
                                 gfloat        delta)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

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

/* ==========================================================================
 * Public API - 0.9.0 3D owner and hooks
 * ========================================================================== */

LrgEntity3D *
lrg_component_get_entity (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), NULL);

    priv = lrg_component_get_instance_private (self);
    return priv->entity;
}

GObject *
lrg_component_get_owner_object (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), NULL);

    priv = lrg_component_get_instance_private (self);

    /* Exactly one of the two weak links is set while owned. */
    if (priv->entity != NULL)
        return (GObject *)priv->entity;
    return (GObject *)priv->owner;
}

LrgWorld3D *
lrg_component_get_world3d (LrgComponent *self)
{
    LrgComponentPrivate *priv;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), NULL);

    priv = lrg_component_get_instance_private (self);
    if (priv->entity == NULL)
        return NULL;
    return lrg_entity3d_get_world (priv->entity);
}

void
lrg_component_fixed_update (LrgComponent *self,
                            gfloat        step)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);
    if (!priv->enabled)
        return;

    klass = LRG_COMPONENT_GET_CLASS (self);
    if (klass->fixed_update != NULL)
        klass->fixed_update (self, step);
}

void
lrg_component_late_update (LrgComponent *self,
                           gfloat        delta)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

    g_return_if_fail (LRG_IS_COMPONENT (self));

    priv = lrg_component_get_instance_private (self);
    if (!priv->enabled)
        return;

    klass = LRG_COMPONENT_GET_CLASS (self);
    if (klass->late_update != NULL)
        klass->late_update (self, delta);
}

void
lrg_component_collect_render (LrgComponent  *self,
                              LrgRenderList *list)
{
    LrgComponentPrivate *priv;
    LrgComponentClass   *klass;

    g_return_if_fail (LRG_IS_COMPONENT (self));
    g_return_if_fail (list != NULL);

    priv = lrg_component_get_instance_private (self);
    if (!priv->enabled)
        return;

    klass = LRG_COMPONENT_GET_CLASS (self);
    if (klass->collect_render != NULL)
        klass->collect_render (self, list);
}

gboolean
lrg_component_get_bounds (LrgComponent     *self,
                          LrgBoundingBox3D *out_local)
{
    LrgComponentClass *klass;

    g_return_val_if_fail (LRG_IS_COMPONENT (self), FALSE);
    g_return_val_if_fail (out_local != NULL, FALSE);

    klass = LRG_COMPONENT_GET_CLASS (self);
    if (klass->get_bounds == NULL)
        return FALSE;
    return klass->get_bounds (self, out_local);
}
