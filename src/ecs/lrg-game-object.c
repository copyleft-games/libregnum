/* lrg-game-object.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Game object with component support.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-game-object.h"
#include "lrg-game-object-private.h"
#include "lrg-component.h"
#include "lrg-component-private.h"
#include "components/lrg-sprite-component.h"
#include "components/lrg-skeleton2d-component.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

typedef struct
{
    GList *components;   /* List of LrgComponent (owned references) */

    /* The instance's GrlUpdatable / GrlDrawable vtables, looked up on the
     * first world update / draw (a subclass may implement either interface
     * itself, so they belong to the instance's final class, which is only
     * known once construction finished). NULL until then; valid for the
     * object's lifetime, since its class outlives it. */
    GrlUpdatableInterface *updatable_iface;
    GrlDrawableInterface  *drawable_iface;
} LrgGameObjectPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgGameObject, lrg_game_object, GRL_TYPE_ENTITY)

/*
 * GrlEntity's own GrlDrawable / GrlUpdatable implementations, read from its
 * vtables at class init. An object whose vtable still holds them (every
 * game object that does not implement those interfaces itself) gets them
 * dispatched without the double work the public path does:
 * grl_entity_get_visible() and GrlEntity's get_visible both return the
 * entity's visible flag (grl_entity_set_visible() is
 * grl_drawable_set_visible()), so one read answers both checks, and
 * GrlEntity's draw only forwards to the class's draw vfunc; likewise
 * active and update. Any other implementation takes the full sequence.
 */
static gboolean (*entity_get_visible) (GrlDrawable  *drawable);
static void     (*entity_draw)        (GrlDrawable  *drawable);
static gboolean (*entity_get_active)  (GrlUpdatable *updatable);
static void     (*entity_update)      (GrlUpdatable *updatable,
                                       gfloat        delta);

/* ==========================================================================
 * GrlEntity Virtual Method Overrides
 * ========================================================================== */

static void
lrg_game_object_update (GrlEntity *entity,
                        gfloat     delta)
{
    LrgGameObject        *self = LRG_GAME_OBJECT (entity);
    LrgGameObjectPrivate *priv = lrg_game_object_get_instance_private (self);
    GList                *l;

    /* Chain up to parent to handle any base entity update logic.  GrlEntity
     * does not always provide an `update' vfunc, so guard the dispatch -- an
     * unconditional NULL vfunc call segfaults (e.g. ticking a world of plain
     * instantiated objects in play-in-editor). */
    {
        GrlEntityClass *parent_class =
            GRL_ENTITY_CLASS (lrg_game_object_parent_class);
        if (parent_class->update != NULL)
            parent_class->update (entity, delta);
    }

    /* Update all components */
    for (l = priv->components; l != NULL; l = l->next)
    {
        LrgComponent *component = LRG_COMPONENT (l->data);

        /* Validated when added (see lrg-component-private.h) */
        _lrg_component_update_unchecked (component, delta);
    }
}

static void
lrg_game_object_draw (GrlEntity *entity)
{
    LrgGameObject        *self = LRG_GAME_OBJECT (entity);
    LrgGameObjectPrivate *priv = lrg_game_object_get_instance_private (self);
    GrlEntityClass       *parent_class;
    GType                 sprite_type;
    GType                 skeleton_type;
    GList                *l;

    /* Chain up when the parent has a draw vfunc. GrlEntity leaves it
     * NULL, and an unconditional call crashed every lrg_world_draw() of a
     * world with objects in it. */
    parent_class = GRL_ENTITY_CLASS (lrg_game_object_parent_class);
    if (parent_class->draw != NULL)
        parent_class->draw (entity);

    /* Draw the object's enabled sprite and 2D skeleton components, in
     * component order.  Both types are final, so an exact type compare is
     * the same test as the LRG_IS_*() macros without the type-system call
     * they make for every other component type. */
    sprite_type = LRG_TYPE_SPRITE_COMPONENT;
    skeleton_type = LRG_TYPE_SKELETON2D_COMPONENT;
    for (l = priv->components; l != NULL; l = l->next)
    {
        LrgComponent *component = l->data;
        GType         type = G_TYPE_FROM_INSTANCE (component);

        if (!_lrg_component_get_enabled_unchecked (component))
            continue;
        if (type == sprite_type)
            lrg_sprite_component_draw (LRG_SPRITE_COMPONENT (component));
        else if (type == skeleton_type)
            lrg_skeleton2d_component_draw (LRG_SKELETON2D_COMPONENT (component));
    }
}

/* ==========================================================================
 * World Dispatch (see lrg-game-object-private.h)
 * ========================================================================== */

/* See lrg-game-object-private.h. */
void
_lrg_game_object_world_update (LrgGameObject *self,
                               gfloat         delta)
{
    LrgGameObjectPrivate  *priv = lrg_game_object_get_instance_private (self);
    GrlUpdatableInterface *iface;
    GrlUpdatable          *updatable = GRL_UPDATABLE (self);

    iface = priv->updatable_iface;
    if (G_UNLIKELY (iface == NULL))
    {
        iface = G_TYPE_INSTANCE_GET_INTERFACE (self, GRL_TYPE_UPDATABLE,
                                               GrlUpdatableInterface);
        priv->updatable_iface = iface;
    }

    /* The entity's active flag, then grl_updatable_update()'s own active
     * check through the vtable: one read when both are the entity's flag */
    if (G_LIKELY (iface->get_active != NULL && iface->get_active == entity_get_active))
    {
        if (!iface->get_active (updatable))
            return;
    }
    else
    {
        if (!grl_entity_get_active (GRL_ENTITY (self)))
            return;
        if (iface->get_active == NULL)
        {
            /* qdata default: leave it to graylib */
            grl_updatable_update (updatable, delta);
            return;
        }
        if (!iface->get_active (updatable))
            return;
    }

    if (G_LIKELY (iface->update != NULL && iface->update == entity_update))
    {
        /* GrlEntity's update: the class vfunc */
        GrlEntityClass *klass = GRL_ENTITY_GET_CLASS (self);

        if (klass->update != NULL)
            klass->update (GRL_ENTITY (self), delta);
    }
    else if (iface->update != NULL)
    {
        iface->update (updatable, delta);
    }
}

/* See lrg-game-object-private.h. */
void
_lrg_game_object_world_draw (LrgGameObject *self)
{
    LrgGameObjectPrivate *priv = lrg_game_object_get_instance_private (self);
    GrlDrawableInterface *iface;
    GrlDrawable          *drawable = GRL_DRAWABLE (self);

    iface = priv->drawable_iface;
    if (G_UNLIKELY (iface == NULL))
    {
        iface = G_TYPE_INSTANCE_GET_INTERFACE (self, GRL_TYPE_DRAWABLE,
                                               GrlDrawableInterface);
        priv->drawable_iface = iface;
    }

    /* The entity's visible flag, then grl_drawable_draw()'s own visible
     * check through the vtable (see update above) */
    if (G_LIKELY (iface->get_visible != NULL && iface->get_visible == entity_get_visible))
    {
        if (!iface->get_visible (drawable))
            return;
    }
    else
    {
        if (!grl_entity_get_visible (GRL_ENTITY (self)))
            return;
        if (iface->get_visible == NULL)
        {
            grl_drawable_draw (drawable);
            return;
        }
        if (!iface->get_visible (drawable))
            return;
    }

    if (G_LIKELY (iface->draw != NULL && iface->draw == entity_draw))
    {
        /* GrlEntity's draw: the class vfunc */
        GrlEntityClass *klass = GRL_ENTITY_GET_CLASS (self);

        if (klass->draw != NULL)
            klass->draw (GRL_ENTITY (self));
    }
    else if (iface->draw != NULL)
    {
        iface->draw (drawable);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_game_object_dispose (GObject *object)
{
    LrgGameObject        *self = LRG_GAME_OBJECT (object);
    LrgGameObjectPrivate *priv = lrg_game_object_get_instance_private (self);

    /* Remove all components before destroying (calls detached on each) */
    lrg_game_object_remove_all_components (self);

    g_clear_pointer (&priv->components, g_list_free);

    G_OBJECT_CLASS (lrg_game_object_parent_class)->dispose (object);
}

static void
lrg_game_object_class_init (LrgGameObjectClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    GrlEntityClass *entity_class = GRL_ENTITY_CLASS (klass);

    object_class->dispose = lrg_game_object_dispose;

    /* Override GrlEntity virtual methods */
    entity_class->update = lrg_game_object_update;
    entity_class->draw = lrg_game_object_draw;

    /* GrlEntity's own interface implementations (see entity_get_visible);
     * its class, vtables included, is complete before ours is set up. */
    {
        gpointer               entity_klass = g_type_class_peek (GRL_TYPE_ENTITY);
        GrlDrawableInterface  *drawable_iface;
        GrlUpdatableInterface *updatable_iface;

        drawable_iface = g_type_interface_peek (entity_klass, GRL_TYPE_DRAWABLE);
        updatable_iface = g_type_interface_peek (entity_klass, GRL_TYPE_UPDATABLE);
        if (drawable_iface != NULL)
        {
            entity_get_visible = drawable_iface->get_visible;
            entity_draw = drawable_iface->draw;
        }
        if (updatable_iface != NULL)
        {
            entity_get_active = updatable_iface->get_active;
            entity_update = updatable_iface->update;
        }
    }
}

static void
lrg_game_object_init (LrgGameObject *self)
{
    LrgGameObjectPrivate *priv = lrg_game_object_get_instance_private (self);

    priv->components = NULL;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
 * lrg_game_object_new:
 *
 * Creates a new game object at position (0, 0).
 *
 * Returns: (transfer full): A new #LrgGameObject
 */
LrgGameObject *
lrg_game_object_new (void)
{
    return g_object_new (LRG_TYPE_GAME_OBJECT, NULL);
}

/*
 * lrg_game_object_new_at:
 * @x: Initial X position
 * @y: Initial Y position
 *
 * Creates a new game object at the specified position.
 *
 * Returns: (transfer full): A new #LrgGameObject
 */
LrgGameObject *
lrg_game_object_new_at (gfloat x,
                        gfloat y)
{
    return g_object_new (LRG_TYPE_GAME_OBJECT,
                         "x", x,
                         "y", y,
                         NULL);
}

/* ==========================================================================
 * Public API - Component Management
 * ========================================================================== */

/*
 * lrg_game_object_add_component:
 * @self: an #LrgGameObject
 * @component: the component to add
 *
 * Adds a component to this game object.
 */
void
lrg_game_object_add_component (LrgGameObject *self,
                               LrgComponent  *component)
{
    LrgGameObjectPrivate *priv;
    LrgGameObject        *existing_owner;

    g_return_if_fail (LRG_IS_GAME_OBJECT (self));
    g_return_if_fail (LRG_IS_COMPONENT (component));

    priv = lrg_game_object_get_instance_private (self);

    /* Check if component is already attached to another game object */
    existing_owner = lrg_component_get_owner (component);
    if (existing_owner != NULL)
    {
        if (existing_owner == self)
        {
            lrg_warning (LRG_LOG_DOMAIN_ECS,
                         "Component %s is already attached to this game object",
                         G_OBJECT_TYPE_NAME (component));
            return;
        }
        else
        {
            lrg_warning (LRG_LOG_DOMAIN_ECS,
                         "Component %s is already attached to another game object; "
                         "detaching first",
                         G_OBJECT_TYPE_NAME (component));
            lrg_game_object_remove_component (existing_owner, component);
        }
    }

    /* Take a reference and add to our list */
    g_object_ref (component);
    priv->components = g_list_append (priv->components, component);

    /* Set the owner (this calls attached() virtual method) */
    _lrg_component_set_owner (component, self);

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Added component %s to game object",
               G_OBJECT_TYPE_NAME (component));
}

/*
 * lrg_game_object_remove_component:
 * @self: an #LrgGameObject
 * @component: the component to remove
 *
 * Removes a component from this game object.
 */
void
lrg_game_object_remove_component (LrgGameObject *self,
                                  LrgComponent  *component)
{
    LrgGameObjectPrivate *priv;
    GList                *link;

    g_return_if_fail (LRG_IS_GAME_OBJECT (self));
    g_return_if_fail (LRG_IS_COMPONENT (component));

    priv = lrg_game_object_get_instance_private (self);

    /* Find the component in our list */
    link = g_list_find (priv->components, component);
    if (link == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Component %s is not attached to this game object",
                     G_OBJECT_TYPE_NAME (component));
        return;
    }

    /* Remove from list */
    priv->components = g_list_delete_link (priv->components, link);

    /* Clear the owner (this calls detached() virtual method) */
    _lrg_component_set_owner (component, NULL);

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Removed component %s from game object",
               G_OBJECT_TYPE_NAME (component));

    /* Release our reference */
    g_object_unref (component);
}

/*
 * lrg_game_object_get_component:
 * @self: an #LrgGameObject
 * @component_type: the #GType of the component to find
 *
 * Finds a component by type.
 *
 * Returns: (transfer none) (nullable): The component, or %NULL if not found
 */
LrgComponent *
lrg_game_object_get_component (LrgGameObject *self,
                               GType          component_type)
{
    LrgGameObjectPrivate *priv;
    GList                *l;

    g_return_val_if_fail (LRG_IS_GAME_OBJECT (self), NULL);
    g_return_val_if_fail (g_type_is_a (component_type, LRG_TYPE_COMPONENT), NULL);

    priv = lrg_game_object_get_instance_private (self);

    for (l = priv->components; l != NULL; l = l->next)
    {
        LrgComponent *component = LRG_COMPONENT (l->data);
        if (G_TYPE_CHECK_INSTANCE_TYPE (component, component_type))
        {
            return component;
        }
    }

    return NULL;
}

/*
 * lrg_game_object_get_components:
 * @self: an #LrgGameObject
 *
 * Gets a list of all components attached to this game object.
 *
 * Returns: (transfer container) (element-type LrgComponent): List of components
 */
GList *
lrg_game_object_get_components (LrgGameObject *self)
{
    LrgGameObjectPrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_OBJECT (self), NULL);

    priv = lrg_game_object_get_instance_private (self);

    return g_list_copy (priv->components);
}

/*
 * lrg_game_object_has_component:
 * @self: an #LrgGameObject
 * @component_type: the #GType to check for
 *
 * Checks if the game object has a component of the specified type.
 *
 * Returns: %TRUE if a component of that type is attached
 */
gboolean
lrg_game_object_has_component (LrgGameObject *self,
                               GType          component_type)
{
    g_return_val_if_fail (LRG_IS_GAME_OBJECT (self), FALSE);

    return lrg_game_object_get_component (self, component_type) != NULL;
}

/*
 * lrg_game_object_get_components_of_type:
 * @self: an #LrgGameObject
 * @component_type: the #GType to find
 *
 * Gets all components of the specified type.
 *
 * Returns: (transfer container) (element-type LrgComponent): List of matching components
 */
GList *
lrg_game_object_get_components_of_type (LrgGameObject *self,
                                        GType          component_type)
{
    LrgGameObjectPrivate *priv;
    GList                *result;
    GList                *l;

    g_return_val_if_fail (LRG_IS_GAME_OBJECT (self), NULL);
    g_return_val_if_fail (g_type_is_a (component_type, LRG_TYPE_COMPONENT), NULL);

    priv = lrg_game_object_get_instance_private (self);
    result = NULL;

    for (l = priv->components; l != NULL; l = l->next)
    {
        LrgComponent *component = LRG_COMPONENT (l->data);
        if (G_TYPE_CHECK_INSTANCE_TYPE (component, component_type))
        {
            result = g_list_prepend (result, component);
        }
    }

    return g_list_reverse (result);
}

/*
 * lrg_game_object_remove_all_components:
 * @self: an #LrgGameObject
 *
 * Removes all components from this game object.
 */
void
lrg_game_object_remove_all_components (LrgGameObject *self)
{
    LrgGameObjectPrivate *priv;
    GList                *l;
    GList                *next;

    g_return_if_fail (LRG_IS_GAME_OBJECT (self));

    priv = lrg_game_object_get_instance_private (self);

    /*
     * Iterate carefully since we're modifying the list.
     * We work on a copy of the list to avoid issues with
     * components that might remove other components in detached().
     */
    for (l = priv->components; l != NULL; l = next)
    {
        LrgComponent *component = LRG_COMPONENT (l->data);
        next = l->next;

        /* Clear owner (calls detached) */
        _lrg_component_set_owner (component, NULL);

        /* Release reference */
        g_object_unref (component);
    }

    g_clear_pointer (&priv->components, g_list_free);
    priv->components = NULL;
}

/*
 * lrg_game_object_get_component_count:
 * @self: an #LrgGameObject
 *
 * Gets the number of components attached to this game object.
 *
 * Returns: The number of attached components
 */
guint
lrg_game_object_get_component_count (LrgGameObject *self)
{
    LrgGameObjectPrivate *priv;

    g_return_val_if_fail (LRG_IS_GAME_OBJECT (self), 0);

    priv = lrg_game_object_get_instance_private (self);

    return g_list_length (priv->components);
}
