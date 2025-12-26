/* lrg-world.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * World container for game objects.
 */

#include "config.h"

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECS

#include "lrg-world.h"
#include "../lrg-log.h"

/* ==========================================================================
 * Private Data
 * ========================================================================== */

struct _LrgWorld
{
    GObject    parent_instance;

    GrlScene  *scene;    /* Underlying graylib scene */
    GList     *objects;  /* List of LrgGameObject (owned references) */
    gboolean   active;   /* Whether the world processes updates/draws */
    gboolean   paused;   /* Whether updates are paused (drawing continues) */
};

G_DEFINE_TYPE (LrgWorld, lrg_world, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ACTIVE,
    PROP_PAUSED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

/*
 * compare_z_index:
 *
 * Comparison function for sorting objects by z-index.
 */
static gint
compare_z_index (gconstpointer a,
                 gconstpointer b)
{
    GrlEntity *entity_a = GRL_ENTITY ((gpointer)a);
    GrlEntity *entity_b = GRL_ENTITY ((gpointer)b);

    return grl_entity_get_z_index (entity_a) - grl_entity_get_z_index (entity_b);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_world_dispose (GObject *object)
{
    LrgWorld *self = LRG_WORLD (object);

    lrg_world_clear (self);

    g_clear_object (&self->scene);

    G_OBJECT_CLASS (lrg_world_parent_class)->dispose (object);
}

static void
lrg_world_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
    LrgWorld *self = LRG_WORLD (object);

    switch (prop_id)
    {
    case PROP_ACTIVE:
        g_value_set_boolean (value, self->active);
        break;
    case PROP_PAUSED:
        g_value_set_boolean (value, self->paused);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_world_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
    LrgWorld *self = LRG_WORLD (object);

    switch (prop_id)
    {
    case PROP_ACTIVE:
        lrg_world_set_active (self, g_value_get_boolean (value));
        break;
    case PROP_PAUSED:
        lrg_world_set_paused (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_world_class_init (LrgWorldClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_world_dispose;
    object_class->get_property = lrg_world_get_property;
    object_class->set_property = lrg_world_set_property;

    /**
     * LrgWorld:active:
     *
     * Whether the world is active.
     *
     * Inactive worlds do not update or draw their objects.
     */
    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether the world processes updates and draws",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgWorld:paused:
     *
     * Whether the world is paused.
     *
     * Paused worlds still draw their objects but do not update them.
     */
    properties[PROP_PAUSED] =
        g_param_spec_boolean ("paused",
                              "Paused",
                              "Whether updates are paused (drawing continues)",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_world_init (LrgWorld *self)
{
    self->scene = grl_scene_new ();
    self->objects = NULL;
    self->active = TRUE;
    self->paused = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_world_new:
 *
 * Creates a new empty world.
 *
 * Returns: (transfer full): A new #LrgWorld
 */
LrgWorld *
lrg_world_new (void)
{
    return g_object_new (LRG_TYPE_WORLD, NULL);
}

/* ==========================================================================
 * Public API - Game Object Management
 * ========================================================================== */

/**
 * lrg_world_add_object:
 * @self: an #LrgWorld
 * @object: the game object to add
 *
 * Adds a game object to this world.
 */
void
lrg_world_add_object (LrgWorld      *self,
                      LrgGameObject *object)
{
    g_return_if_fail (LRG_IS_WORLD (self));
    g_return_if_fail (LRG_IS_GAME_OBJECT (object));

    /* Check if already in the world */
    if (g_list_find (self->objects, object) != NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Game object is already in this world");
        return;
    }

    /* Take reference and add to list */
    g_object_ref (object);
    self->objects = g_list_append (self->objects, object);

    /* Add to graylib scene as well */
    grl_scene_add_entity (self->scene, GRL_ENTITY (object));

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Added game object to world (count: %u)",
               g_list_length (self->objects));
}

/**
 * lrg_world_remove_object:
 * @self: an #LrgWorld
 * @object: the game object to remove
 *
 * Removes a game object from this world.
 */
void
lrg_world_remove_object (LrgWorld      *self,
                         LrgGameObject *object)
{
    GList *link;

    g_return_if_fail (LRG_IS_WORLD (self));
    g_return_if_fail (LRG_IS_GAME_OBJECT (object));

    link = g_list_find (self->objects, object);
    if (link == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Game object is not in this world");
        return;
    }

    /* Remove from list */
    self->objects = g_list_delete_link (self->objects, link);

    /* Remove from graylib scene */
    grl_scene_remove_entity (self->scene, GRL_ENTITY (object));

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Removed game object from world (count: %u)",
               g_list_length (self->objects));

    /* Release reference */
    g_object_unref (object);
}

/**
 * lrg_world_clear:
 * @self: an #LrgWorld
 *
 * Removes all game objects from this world.
 */
void
lrg_world_clear (LrgWorld *self)
{
    GList *l;

    g_return_if_fail (LRG_IS_WORLD (self));

    /* Remove all objects from scene and release references */
    for (l = self->objects; l != NULL; l = l->next)
    {
        LrgGameObject *object = LRG_GAME_OBJECT (l->data);
        grl_scene_remove_entity (self->scene, GRL_ENTITY (object));
        g_object_unref (object);
    }

    g_clear_pointer (&self->objects, g_list_free);
    self->objects = NULL;

    lrg_debug (LRG_LOG_DOMAIN_ECS, "World cleared");
}

/**
 * lrg_world_get_objects:
 * @self: an #LrgWorld
 *
 * Gets a list of all game objects in this world.
 *
 * Returns: (transfer container) (element-type LrgGameObject): List of objects
 */
GList *
lrg_world_get_objects (LrgWorld *self)
{
    g_return_val_if_fail (LRG_IS_WORLD (self), NULL);

    return g_list_copy (self->objects);
}

/**
 * lrg_world_get_object_count:
 * @self: an #LrgWorld
 *
 * Gets the number of game objects in this world.
 *
 * Returns: The number of objects
 */
guint
lrg_world_get_object_count (LrgWorld *self)
{
    g_return_val_if_fail (LRG_IS_WORLD (self), 0);

    return g_list_length (self->objects);
}

/* ==========================================================================
 * Public API - Object Lookup
 * ========================================================================== */

/**
 * lrg_world_find_by_tag:
 * @self: an #LrgWorld
 * @tag: the tag to search for
 *
 * Finds the first game object with the specified tag.
 *
 * Returns: (transfer none) (nullable): The game object, or %NULL if not found
 */
LrgGameObject *
lrg_world_find_by_tag (LrgWorld    *self,
                       const gchar *tag)
{
    GList *l;

    g_return_val_if_fail (LRG_IS_WORLD (self), NULL);
    g_return_val_if_fail (tag != NULL, NULL);

    for (l = self->objects; l != NULL; l = l->next)
    {
        LrgGameObject *object = LRG_GAME_OBJECT (l->data);
        if (grl_entity_has_tag (GRL_ENTITY (object), tag))
        {
            return object;
        }
    }

    return NULL;
}

/**
 * lrg_world_find_all_by_tag:
 * @self: an #LrgWorld
 * @tag: the tag to search for
 *
 * Finds all game objects with the specified tag.
 *
 * Returns: (transfer container) (element-type LrgGameObject): List of matching objects
 */
GList *
lrg_world_find_all_by_tag (LrgWorld    *self,
                           const gchar *tag)
{
    GList *result;
    GList *l;

    g_return_val_if_fail (LRG_IS_WORLD (self), NULL);
    g_return_val_if_fail (tag != NULL, NULL);

    result = NULL;

    for (l = self->objects; l != NULL; l = l->next)
    {
        LrgGameObject *object = LRG_GAME_OBJECT (l->data);
        if (grl_entity_has_tag (GRL_ENTITY (object), tag))
        {
            result = g_list_prepend (result, object);
        }
    }

    return g_list_reverse (result);
}

/* ==========================================================================
 * Public API - Frame Processing
 * ========================================================================== */

/**
 * lrg_world_update:
 * @self: an #LrgWorld
 * @delta: time elapsed since last frame in seconds
 *
 * Updates all game objects in the world.
 */
void
lrg_world_update (LrgWorld *self,
                  gfloat    delta)
{
    GList *l;

    g_return_if_fail (LRG_IS_WORLD (self));

    /* Don't update if inactive or paused */
    if (!self->active || self->paused)
    {
        return;
    }

    /* Update all active objects */
    for (l = self->objects; l != NULL; l = l->next)
    {
        LrgGameObject *object = LRG_GAME_OBJECT (l->data);
        if (grl_entity_get_active (GRL_ENTITY (object)))
        {
            grl_updatable_update (GRL_UPDATABLE (object), delta);
        }
    }
}

/**
 * lrg_world_draw:
 * @self: an #LrgWorld
 *
 * Draws all visible game objects in the world.
 */
void
lrg_world_draw (LrgWorld *self)
{
    GList *sorted;
    GList *l;

    g_return_if_fail (LRG_IS_WORLD (self));

    /* Don't draw if inactive */
    if (!self->active)
    {
        return;
    }

    /* Sort objects by z-index for proper draw order */
    sorted = g_list_copy (self->objects);
    sorted = g_list_sort (sorted, compare_z_index);

    /* Draw all visible objects */
    for (l = sorted; l != NULL; l = l->next)
    {
        LrgGameObject *object = LRG_GAME_OBJECT (l->data);
        if (grl_entity_get_visible (GRL_ENTITY (object)))
        {
            grl_drawable_draw (GRL_DRAWABLE (object));
        }
    }

    g_list_free (sorted);
}

/* ==========================================================================
 * Public API - graylib Integration
 * ========================================================================== */

/**
 * lrg_world_get_scene:
 * @self: an #LrgWorld
 *
 * Gets the underlying graylib scene.
 *
 * Returns: (transfer none): The #GrlScene
 */
GrlScene *
lrg_world_get_scene (LrgWorld *self)
{
    g_return_val_if_fail (LRG_IS_WORLD (self), NULL);

    return self->scene;
}

/* ==========================================================================
 * Public API - Properties
 * ========================================================================== */

/**
 * lrg_world_get_active:
 * @self: an #LrgWorld
 *
 * Gets whether the world is active.
 *
 * Returns: %TRUE if the world is active
 */
gboolean
lrg_world_get_active (LrgWorld *self)
{
    g_return_val_if_fail (LRG_IS_WORLD (self), FALSE);

    return self->active;
}

/**
 * lrg_world_set_active:
 * @self: an #LrgWorld
 * @active: whether the world should be active
 *
 * Sets whether the world is active.
 */
void
lrg_world_set_active (LrgWorld *self,
                      gboolean  active)
{
    g_return_if_fail (LRG_IS_WORLD (self));

    active = !!active;

    if (self->active != active)
    {
        self->active = active;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

/**
 * lrg_world_get_paused:
 * @self: an #LrgWorld
 *
 * Gets whether the world is paused.
 *
 * Returns: %TRUE if the world is paused
 */
gboolean
lrg_world_get_paused (LrgWorld *self)
{
    g_return_val_if_fail (LRG_IS_WORLD (self), FALSE);

    return self->paused;
}

/**
 * lrg_world_set_paused:
 * @self: an #LrgWorld
 * @paused: whether the world should be paused
 *
 * Sets whether the world is paused.
 */
void
lrg_world_set_paused (LrgWorld *self,
                      gboolean  paused)
{
    g_return_if_fail (LRG_IS_WORLD (self));

    paused = !!paused;

    if (self->paused != paused)
    {
        self->paused = paused;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSED]);
    }
}
