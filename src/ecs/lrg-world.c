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

    GrlScene   *scene;        /* Underlying graylib scene */
    GList      *objects;      /* List of LrgGameObject (owned references) */
    GList      *objects_tail; /* Last link of @objects, for O(1) append */
    GHashTable *object_set;   /* object -> its link in @objects (no refs) */
    guint       object_count; /* Length of @objects */
    gboolean    active;       /* Whether the world processes updates/draws */
    gboolean    paused;       /* Whether updates are paused (drawing continues) */

    /* Draw order, kept between frames: entries sorted by (z-index,
     * position in @objects). Rebuilt when objects come or go, otherwise
     * re-sorted in place, which is linear while z-indices are stable. */
    GArray     *draw_order;   /* DrawEntry */
    gboolean    draw_order_dirty;
};

typedef struct
{
    LrgGameObject *object;
    guint          seq;   /* index in @objects when the order was built */
    gint           z;     /* z-index sampled this frame */
} DrawEntry;

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
 * draw_entry_before:
 *
 * Strict "draws before" order: lower z-index first, insertion order among
 * equal z-indices (what a stable sort of @objects by z-index gives).
 */
static inline gboolean
draw_entry_before (const DrawEntry *a,
                   const DrawEntry *b)
{
    if (a->z != b->z)
        return a->z < b->z;
    return a->seq < b->seq;
}

static gint
draw_entry_compare (gconstpointer a,
                    gconstpointer b)
{
    const DrawEntry *ea = a;
    const DrawEntry *eb = b;

    if (draw_entry_before (ea, eb))
        return -1;
    return draw_entry_before (eb, ea) ? 1 : 0;
}

/*
 * world_update_draw_order:
 *
 * Brings @self->draw_order up to date. When membership changed it is
 * rebuilt from the object list with each object's z-index and sorted
 * (O(n log n); rebuilding with every z at 0 and insertion sorting was
 * O(n^2) for mixed z-indices, a multi-millisecond hitch on the frame after
 * a wave of spawns or deaths). Otherwise every z-index is resampled and
 * the order insertion sorted, a single linear pass while objects keep
 * their z-index.
 */
static void
world_update_draw_order (LrgWorld *self)
{
    DrawEntry *entries;
    GList     *l;
    guint      n;
    guint      i;

    if (self->draw_order_dirty)
    {
        g_array_set_size (self->draw_order, 0);
        for (l = self->objects, i = 0; l != NULL; l = l->next, i++)
        {
            DrawEntry entry;

            entry.object = (LrgGameObject *)l->data;
            entry.seq = i;
            entry.z = grl_entity_get_z_index (GRL_ENTITY (entry.object));
            g_array_append_val (self->draw_order, entry);
        }
        /* (seq is unique, so the order is total and qsort's instability
         * does not matter) */
        g_array_sort (self->draw_order, draw_entry_compare);
        self->draw_order_dirty = FALSE;
        return;
    }

    entries = (DrawEntry *)(gpointer)self->draw_order->data;
    n = self->draw_order->len;

    for (i = 0; i < n; i++)
        entries[i].z = grl_entity_get_z_index (GRL_ENTITY (entries[i].object));

    for (i = 1; i < n; i++)
    {
        DrawEntry key;
        guint     j;

        if (!draw_entry_before (&entries[i], &entries[i - 1]))
            continue;

        key = entries[i];
        j = i;
        while (j > 0 && draw_entry_before (&key, &entries[j - 1]))
        {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = key;
    }
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
    g_clear_pointer (&self->object_set, g_hash_table_unref);
    g_clear_pointer (&self->draw_order, g_array_unref);

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
    self->objects_tail = NULL;
    self->object_set = g_hash_table_new (g_direct_hash, g_direct_equal);
    self->object_count = 0;
    self->draw_order = g_array_new (FALSE, FALSE, sizeof (DrawEntry));
    self->draw_order_dirty = TRUE;
    self->active = TRUE;
    self->paused = FALSE;
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/*
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

/*
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
    if (g_hash_table_contains (self->object_set, object))
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Game object is already in this world");
        return;
    }

    /* Take reference and add to list */
    g_object_ref (object);
    if (self->objects_tail == NULL)
    {
        self->objects = g_list_append (NULL, object);
        self->objects_tail = self->objects;
    }
    else
    {
        /* g_list_append() on the tail link is O(1) */
        self->objects_tail = g_list_append (self->objects_tail, object)->next;
    }
    g_hash_table_insert (self->object_set, object, self->objects_tail);
    self->object_count++;
    self->draw_order_dirty = TRUE;

    /* Add to graylib scene as well */
    grl_scene_add_entity (self->scene, GRL_ENTITY (object));

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Added game object to world (count: %u)",
               self->object_count);
}

/*
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

    /* The stored link makes this O(1): removing a wave of objects in one
     * frame no longer walks the list once per object. */
    link = g_hash_table_lookup (self->object_set, object);
    if (link == NULL)
    {
        lrg_warning (LRG_LOG_DOMAIN_ECS,
                     "Game object is not in this world");
        return;
    }

    /* Remove from list */
    if (link == self->objects_tail)
        self->objects_tail = link->prev;
    self->objects = g_list_delete_link (self->objects, link);
    g_hash_table_remove (self->object_set, object);
    self->object_count--;
    self->draw_order_dirty = TRUE;

    /* Remove from graylib scene */
    grl_scene_remove_entity (self->scene, GRL_ENTITY (object));

    lrg_debug (LRG_LOG_DOMAIN_ECS,
               "Removed game object from world (count: %u)",
               self->object_count);

    /* Release reference */
    g_object_unref (object);
}

/*
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
    self->objects_tail = NULL;
    self->object_count = 0;
    if (self->object_set != NULL)
        g_hash_table_remove_all (self->object_set);
    self->draw_order_dirty = TRUE;

    lrg_debug (LRG_LOG_DOMAIN_ECS, "World cleared");
}

/*
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

/*
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

    return self->object_count;
}

/* ==========================================================================
 * Public API - Object Lookup
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
 * lrg_world_draw:
 * @self: an #LrgWorld
 *
 * Draws all visible game objects in the world.
 */
void
lrg_world_draw (LrgWorld *self)
{
    const DrawEntry *entries;
    guint            i;

    g_return_if_fail (LRG_IS_WORLD (self));

    /* Don't draw if inactive */
    if (!self->active)
    {
        return;
    }

    /* Sort objects by z-index for proper draw order (stable) */
    world_update_draw_order (self);

    /* Draw all visible objects. Drawing must not add or remove objects;
     * the order array is only rebuilt at the start of the next draw. */
    entries = (const DrawEntry *)(gconstpointer)self->draw_order->data;
    for (i = 0; i < self->draw_order->len; i++)
    {
        GrlEntity *entity = GRL_ENTITY (entries[i].object);

        if (grl_entity_get_visible (entity))
        {
            grl_drawable_draw (GRL_DRAWABLE (entity));
        }
    }
}

/* ==========================================================================
 * Public API - graylib Integration
 * ========================================================================== */

/*
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

/*
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

/*
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

/*
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

/*
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
