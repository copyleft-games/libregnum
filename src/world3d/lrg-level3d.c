/* lrg-level3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * 3D level implementation.
 */

#include "config.h"
#include "lrg-level3d.h"
#include "../lrg-enums.h"

/* Default level bounds (large world) */
#define DEFAULT_LEVEL_SIZE 10000.0f

static void
free_gvalue (gpointer data)
{
    GValue *value = data;

    g_value_unset (value);
    g_free (value);
}

struct _LrgLevel3D
{
    GObject          parent_instance;

    gchar           *id;
    gchar           *name;
    LrgBoundingBox3D bounds;

    GHashTable      *spawn_points;   /* gchar* -> LrgSpawnPoint3D* */
    GHashTable      *triggers;       /* gchar* -> LrgTrigger3D* */
    GPtrArray       *models;         /* GrlModel* */
    GHashTable      *model_bounds;   /* GrlModel* -> LrgBoundingBox3D* */

    LrgOctree       *octree;
    GHashTable      *properties;     /* gchar* -> GValue* */
};

G_DEFINE_FINAL_TYPE (LrgLevel3D, lrg_level3d, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_ID,
    PROP_NAME,
    PROP_SPAWN_POINT_COUNT,
    PROP_TRIGGER_COUNT,
    PROP_MODEL_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_level3d_finalize (GObject *object)
{
    LrgLevel3D *self = LRG_LEVEL3D (object);

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->name, g_free);
    g_clear_pointer (&self->spawn_points, g_hash_table_unref);
    g_clear_pointer (&self->triggers, g_hash_table_unref);
    g_clear_pointer (&self->models, g_ptr_array_unref);
    g_clear_pointer (&self->model_bounds, g_hash_table_unref);
    g_clear_object (&self->octree);
    g_clear_pointer (&self->properties, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_level3d_parent_class)->finalize (object);
}

static void
lrg_level3d_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
    LrgLevel3D *self = LRG_LEVEL3D (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_value_set_string (value, self->id);
        break;
    case PROP_NAME:
        g_value_set_string (value, self->name);
        break;
    case PROP_SPAWN_POINT_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->spawn_points));
        break;
    case PROP_TRIGGER_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->triggers));
        break;
    case PROP_MODEL_COUNT:
        g_value_set_uint (value, self->models->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_level3d_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
    LrgLevel3D *self = LRG_LEVEL3D (object);

    switch (prop_id)
    {
    case PROP_ID:
        g_free (self->id);
        self->id = g_value_dup_string (value);
        break;
    case PROP_NAME:
        g_free (self->name);
        self->name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_level3d_class_init (LrgLevel3DClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_level3d_finalize;
    object_class->get_property = lrg_level3d_get_property;
    object_class->set_property = lrg_level3d_set_property;

    /**
     * LrgLevel3D:id:
     *
     * The unique identifier for this level.
     */
    properties[PROP_ID] =
        g_param_spec_string ("id",
                             "ID",
                             "Level identifier",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgLevel3D:name:
     *
     * The display name of the level.
     */
    properties[PROP_NAME] =
        g_param_spec_string ("name",
                             "Name",
                             "Display name",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLevel3D:spawn-point-count:
     *
     * The number of spawn points in the level.
     */
    properties[PROP_SPAWN_POINT_COUNT] =
        g_param_spec_uint ("spawn-point-count",
                           "Spawn Point Count",
                           "Number of spawn points",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLevel3D:trigger-count:
     *
     * The number of triggers in the level.
     */
    properties[PROP_TRIGGER_COUNT] =
        g_param_spec_uint ("trigger-count",
                           "Trigger Count",
                           "Number of triggers",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgLevel3D:model-count:
     *
     * The number of models in the level.
     */
    properties[PROP_MODEL_COUNT] =
        g_param_spec_uint ("model-count",
                           "Model Count",
                           "Number of models",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_level3d_init (LrgLevel3D *self)
{
    /* Default bounds */
    self->bounds.min.x = -DEFAULT_LEVEL_SIZE;
    self->bounds.min.y = -DEFAULT_LEVEL_SIZE;
    self->bounds.min.z = -DEFAULT_LEVEL_SIZE;
    self->bounds.max.x = DEFAULT_LEVEL_SIZE;
    self->bounds.max.y = DEFAULT_LEVEL_SIZE;
    self->bounds.max.z = DEFAULT_LEVEL_SIZE;

    self->spawn_points = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                 g_free, (GDestroyNotify)lrg_spawn_point3d_free);
    self->triggers = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, (GDestroyNotify)lrg_trigger3d_free);
    self->models = g_ptr_array_new_with_free_func (g_object_unref);
    self->model_bounds = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                 NULL, (GDestroyNotify)lrg_bounding_box3d_free);
    self->octree = lrg_octree_new (&self->bounds);
    self->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, free_gvalue);
}

/**
 * lrg_level3d_new:
 * @id: Unique identifier for this level
 *
 * Creates a new 3D level.
 *
 * Returns: (transfer full): A new #LrgLevel3D
 */
LrgLevel3D *
lrg_level3d_new (const gchar *id)
{
    g_return_val_if_fail (id != NULL, NULL);

    return g_object_new (LRG_TYPE_LEVEL3D,
                         "id", id,
                         NULL);
}

/**
 * lrg_level3d_get_id:
 * @self: An #LrgLevel3D
 *
 * Gets the level identifier.
 *
 * Returns: (transfer none): The level ID
 */
const gchar *
lrg_level3d_get_id (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    return self->id;
}

/**
 * lrg_level3d_get_name:
 * @self: An #LrgLevel3D
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The display name
 */
const gchar *
lrg_level3d_get_name (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    return self->name;
}

/**
 * lrg_level3d_set_name:
 * @self: An #LrgLevel3D
 * @name: (nullable): Display name
 *
 * Sets the display name.
 */
void
lrg_level3d_set_name (LrgLevel3D  *self,
                      const gchar *name)
{
    g_return_if_fail (LRG_IS_LEVEL3D (self));

    if (g_strcmp0 (self->name, name) != 0)
    {
        g_free (self->name);
        self->name = g_strdup (name);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NAME]);
    }
}

/**
 * lrg_level3d_get_bounds:
 * @self: An #LrgLevel3D
 *
 * Gets the level bounds.
 *
 * Returns: (transfer full): The bounds
 */
LrgBoundingBox3D *
lrg_level3d_get_bounds (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    return lrg_bounding_box3d_copy (&self->bounds);
}

/**
 * lrg_level3d_set_bounds:
 * @self: An #LrgLevel3D
 * @bounds: (transfer none): New level bounds
 *
 * Sets the level bounds. This reinitializes the internal octree.
 */
void
lrg_level3d_set_bounds (LrgLevel3D             *self,
                        const LrgBoundingBox3D *bounds)
{
    g_return_if_fail (LRG_IS_LEVEL3D (self));
    g_return_if_fail (bounds != NULL);

    self->bounds = *bounds;

    /* Reinitialize octree with new bounds */
    g_clear_object (&self->octree);
    self->octree = lrg_octree_new (&self->bounds);

    /* Re-add all models to octree */
    lrg_level3d_rebuild_octree (self);
}

/* --- Spawn Point Management --- */

/**
 * lrg_level3d_add_spawn_point:
 * @self: An #LrgLevel3D
 * @spawn: (transfer none): Spawn point to add
 *
 * Adds a spawn point to the level.
 */
void
lrg_level3d_add_spawn_point (LrgLevel3D            *self,
                             const LrgSpawnPoint3D *spawn)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_LEVEL3D (self));
    g_return_if_fail (spawn != NULL);

    id = lrg_spawn_point3d_get_id (spawn);
    g_hash_table_insert (self->spawn_points,
                         g_strdup (id),
                         lrg_spawn_point3d_copy (spawn));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPAWN_POINT_COUNT]);
}

/**
 * lrg_level3d_remove_spawn_point:
 * @self: An #LrgLevel3D
 * @id: Spawn point ID to remove
 *
 * Removes a spawn point from the level.
 *
 * Returns: %TRUE if the spawn point was found and removed
 */
gboolean
lrg_level3d_remove_spawn_point (LrgLevel3D  *self,
                                const gchar *id)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    removed = g_hash_table_remove (self->spawn_points, id);

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPAWN_POINT_COUNT]);

    return removed;
}

/**
 * lrg_level3d_get_spawn_point:
 * @self: An #LrgLevel3D
 * @id: Spawn point ID
 *
 * Gets a spawn point by ID.
 *
 * Returns: (transfer none) (nullable): The spawn point, or %NULL if not found
 */
const LrgSpawnPoint3D *
lrg_level3d_get_spawn_point (LrgLevel3D  *self,
                             const gchar *id)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->spawn_points, id);
}

/**
 * lrg_level3d_get_spawn_points:
 * @self: An #LrgLevel3D
 *
 * Gets all spawn points.
 *
 * Returns: (transfer container) (element-type LrgSpawnPoint3D): Array of spawn points
 */
GPtrArray *
lrg_level3d_get_spawn_points (LrgLevel3D *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->spawn_points);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (result, value);

    return result;
}

/**
 * lrg_level3d_get_spawn_points_by_type:
 * @self: An #LrgLevel3D
 * @spawn_type: Type of spawn points to get
 *
 * Gets spawn points of a specific type.
 *
 * Returns: (transfer container) (element-type LrgSpawnPoint3D): Array of matching spawn points
 */
GPtrArray *
lrg_level3d_get_spawn_points_by_type (LrgLevel3D   *self,
                                      LrgSpawnType  spawn_type)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->spawn_points);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgSpawnPoint3D *spawn = value;
        if (lrg_spawn_point3d_get_spawn_type (spawn) == spawn_type)
            g_ptr_array_add (result, spawn);
    }

    return result;
}

/**
 * lrg_level3d_get_spawn_point_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of spawn points.
 *
 * Returns: Spawn point count
 */
guint
lrg_level3d_get_spawn_point_count (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), 0);

    return g_hash_table_size (self->spawn_points);
}

/* --- Trigger Management --- */

/**
 * lrg_level3d_add_trigger:
 * @self: An #LrgLevel3D
 * @trigger: (transfer none): Trigger to add
 *
 * Adds a trigger to the level.
 */
void
lrg_level3d_add_trigger (LrgLevel3D         *self,
                         const LrgTrigger3D *trigger)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_LEVEL3D (self));
    g_return_if_fail (trigger != NULL);

    id = lrg_trigger3d_get_id (trigger);
    g_hash_table_insert (self->triggers,
                         g_strdup (id),
                         lrg_trigger3d_copy (trigger));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);
}

/**
 * lrg_level3d_remove_trigger:
 * @self: An #LrgLevel3D
 * @id: Trigger ID to remove
 *
 * Removes a trigger from the level.
 *
 * Returns: %TRUE if the trigger was found and removed
 */
gboolean
lrg_level3d_remove_trigger (LrgLevel3D  *self,
                            const gchar *id)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    removed = g_hash_table_remove (self->triggers, id);

    if (removed)
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);

    return removed;
}

/**
 * lrg_level3d_get_trigger:
 * @self: An #LrgLevel3D
 * @id: Trigger ID
 *
 * Gets a trigger by ID.
 *
 * Returns: (transfer none) (nullable): The trigger, or %NULL if not found
 */
const LrgTrigger3D *
lrg_level3d_get_trigger (LrgLevel3D  *self,
                         const gchar *id)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->triggers, id);
}

/**
 * lrg_level3d_get_triggers:
 * @self: An #LrgLevel3D
 *
 * Gets all triggers.
 *
 * Returns: (transfer container) (element-type LrgTrigger3D): Array of triggers
 */
GPtrArray *
lrg_level3d_get_triggers (LrgLevel3D *self)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->triggers);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        g_ptr_array_add (result, value);

    return result;
}

/**
 * lrg_level3d_get_trigger_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of triggers.
 *
 * Returns: Trigger count
 */
guint
lrg_level3d_get_trigger_count (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), 0);

    return g_hash_table_size (self->triggers);
}

/**
 * lrg_level3d_check_triggers:
 * @self: An #LrgLevel3D
 * @point: (transfer none): Point to test
 *
 * Finds all enabled triggers that contain the given point.
 *
 * Returns: (transfer container) (element-type LrgTrigger3D): Array of activated triggers
 */
GPtrArray *
lrg_level3d_check_triggers (LrgLevel3D       *self,
                            const GrlVector3 *point)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (point != NULL, NULL);

    result = g_ptr_array_new ();

    g_hash_table_iter_init (&iter, self->triggers);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgTrigger3D *trigger = value;
        if (lrg_trigger3d_test_point (trigger, point))
            g_ptr_array_add (result, trigger);
    }

    return result;
}

/* --- Model Management --- */

/**
 * lrg_level3d_add_model:
 * @self: An #LrgLevel3D
 * @model: (transfer none): Model to add
 * @bounds: (transfer none): Model bounds for spatial indexing
 *
 * Adds a model to the level.
 */
void
lrg_level3d_add_model (LrgLevel3D             *self,
                       GrlModel               *model,
                       const LrgBoundingBox3D *bounds)
{
    g_return_if_fail (LRG_IS_LEVEL3D (self));
    g_return_if_fail (GRL_IS_MODEL (model));
    g_return_if_fail (bounds != NULL);

    g_ptr_array_add (self->models, g_object_ref (model));
    g_hash_table_insert (self->model_bounds, model, lrg_bounding_box3d_copy (bounds));
    lrg_octree_insert (self->octree, model, bounds);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL_COUNT]);
}

/**
 * lrg_level3d_remove_model:
 * @self: An #LrgLevel3D
 * @model: Model to remove
 *
 * Removes a model from the level.
 *
 * Returns: %TRUE if the model was found and removed
 */
gboolean
lrg_level3d_remove_model (LrgLevel3D *self,
                          GrlModel   *model)
{
    gboolean removed;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), FALSE);
    g_return_val_if_fail (GRL_IS_MODEL (model), FALSE);

    removed = g_ptr_array_remove (self->models, model);

    if (removed)
    {
        g_hash_table_remove (self->model_bounds, model);
        lrg_octree_remove (self->octree, model);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL_COUNT]);
    }

    return removed;
}

/**
 * lrg_level3d_get_models:
 * @self: An #LrgLevel3D
 *
 * Gets all models in the level.
 *
 * Returns: (transfer container) (element-type GrlModel): Array of models
 */
GPtrArray *
lrg_level3d_get_models (LrgLevel3D *self)
{
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    result = g_ptr_array_new ();

    for (i = 0; i < self->models->len; i++)
        g_ptr_array_add (result, g_ptr_array_index (self->models, i));

    return result;
}

/**
 * lrg_level3d_get_model_count:
 * @self: An #LrgLevel3D
 *
 * Gets the number of models.
 *
 * Returns: Model count
 */
guint
lrg_level3d_get_model_count (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), 0);

    return self->models->len;
}

/* --- Spatial Queries --- */

/**
 * lrg_level3d_query_box:
 * @self: An #LrgLevel3D
 * @box: (transfer none): Query bounding box
 *
 * Finds all objects (models) that intersect with the query box.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
GPtrArray *
lrg_level3d_query_box (LrgLevel3D             *self,
                       const LrgBoundingBox3D *box)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (box != NULL, NULL);

    return lrg_octree_query_box (self->octree, box);
}

/**
 * lrg_level3d_query_sphere:
 * @self: An #LrgLevel3D
 * @center: (transfer none): Sphere center
 * @radius: Sphere radius
 *
 * Finds all objects (models) that intersect with a sphere.
 *
 * Returns: (transfer container) (element-type gpointer): Array of objects
 */
GPtrArray *
lrg_level3d_query_sphere (LrgLevel3D       *self,
                          const GrlVector3 *center,
                          gfloat            radius)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (center != NULL, NULL);

    return lrg_octree_query_sphere (self->octree, center, radius);
}

/* --- Properties --- */

/**
 * lrg_level3d_set_property_value:
 * @self: An #LrgLevel3D
 * @key: Property key
 * @value: (transfer none): Property value
 *
 * Sets a custom property on the level.
 */
void
lrg_level3d_set_property_value (LrgLevel3D   *self,
                                const gchar  *key,
                                const GValue *value)
{
    GValue *new_value;

    g_return_if_fail (LRG_IS_LEVEL3D (self));
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    new_value = g_new0 (GValue, 1);
    g_value_init (new_value, G_VALUE_TYPE (value));
    g_value_copy (value, new_value);

    g_hash_table_insert (self->properties, g_strdup (key), new_value);
}

/**
 * lrg_level3d_get_property_value:
 * @self: An #LrgLevel3D
 * @key: Property key
 *
 * Gets a custom property from the level.
 *
 * Returns: (transfer none) (nullable): The property value, or %NULL if not found
 */
const GValue *
lrg_level3d_get_property_value (LrgLevel3D  *self,
                                const gchar *key)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->properties, key);
}

/**
 * lrg_level3d_has_property:
 * @self: An #LrgLevel3D
 * @key: Property key
 *
 * Checks if a property is set.
 *
 * Returns: %TRUE if the property exists
 */
gboolean
lrg_level3d_has_property (LrgLevel3D  *self,
                          const gchar *key)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->properties, key);
}

/**
 * lrg_level3d_get_property_keys:
 * @self: An #LrgLevel3D
 *
 * Gets all property keys.
 *
 * Returns: (transfer container) (element-type utf8): List of property keys
 */
GList *
lrg_level3d_get_property_keys (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    return g_hash_table_get_keys (self->properties);
}

/* --- Octree Access --- */

/**
 * lrg_level3d_get_octree:
 * @self: An #LrgLevel3D
 *
 * Gets the internal octree for advanced spatial queries.
 *
 * Returns: (transfer none): The internal octree
 */
LrgOctree *
lrg_level3d_get_octree (LrgLevel3D *self)
{
    g_return_val_if_fail (LRG_IS_LEVEL3D (self), NULL);

    return self->octree;
}

/**
 * lrg_level3d_rebuild_octree:
 * @self: An #LrgLevel3D
 *
 * Rebuilds the internal octree.
 * Call this after making many model changes.
 */
void
lrg_level3d_rebuild_octree (LrgLevel3D *self)
{
    guint i;

    g_return_if_fail (LRG_IS_LEVEL3D (self));

    lrg_octree_clear (self->octree);

    for (i = 0; i < self->models->len; i++)
    {
        GrlModel *model = g_ptr_array_index (self->models, i);
        LrgBoundingBox3D *bounds = g_hash_table_lookup (self->model_bounds, model);

        if (bounds != NULL)
            lrg_octree_insert (self->octree, model, bounds);
    }
}

/* --- Utility --- */

/**
 * lrg_level3d_clear:
 * @self: An #LrgLevel3D
 *
 * Removes all content from the level.
 */
void
lrg_level3d_clear (LrgLevel3D *self)
{
    g_return_if_fail (LRG_IS_LEVEL3D (self));

    g_hash_table_remove_all (self->spawn_points);
    g_hash_table_remove_all (self->triggers);
    g_ptr_array_set_size (self->models, 0);
    g_hash_table_remove_all (self->model_bounds);
    lrg_octree_clear (self->octree);
    g_hash_table_remove_all (self->properties);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SPAWN_POINT_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TRIGGER_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL_COUNT]);
}
