/* lrg-spawn-point3d.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spawn point implementation.
 */

#include "config.h"
#include "lrg-spawn-point3d.h"

struct _LrgSpawnPoint3D
{
    gchar       *id;
    GrlVector3   position;
    GrlVector3   rotation;
    LrgSpawnType spawn_type;
    gchar       *entity_type;
    GHashTable  *properties;    /* gchar* -> GValue* */
};

static void
free_gvalue (gpointer data)
{
    GValue *value = data;

    g_value_unset (value);
    g_free (value);
}

/* Register as a GBoxed type */
G_DEFINE_BOXED_TYPE (LrgSpawnPoint3D, lrg_spawn_point3d,
                     lrg_spawn_point3d_copy, lrg_spawn_point3d_free)

/**
 * lrg_spawn_point3d_new:
 * @id: Unique identifier for this spawn point
 * @x: X position
 * @y: Y position
 * @z: Z position
 * @spawn_type: Type of spawn point
 *
 * Creates a new spawn point.
 *
 * Returns: (transfer full): A newly allocated #LrgSpawnPoint3D
 */
LrgSpawnPoint3D *
lrg_spawn_point3d_new (const gchar  *id,
                       gfloat        x,
                       gfloat        y,
                       gfloat        z,
                       LrgSpawnType  spawn_type)
{
    LrgSpawnPoint3D *self;

    g_return_val_if_fail (id != NULL, NULL);

    self = g_new0 (LrgSpawnPoint3D, 1);
    self->id = g_strdup (id);
    self->position.x = x;
    self->position.y = y;
    self->position.z = z;
    self->rotation.x = 0.0f;
    self->rotation.y = 0.0f;
    self->rotation.z = 0.0f;
    self->spawn_type = spawn_type;
    self->entity_type = NULL;
    self->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, free_gvalue);

    return self;
}

/**
 * lrg_spawn_point3d_new_from_vector:
 * @id: Unique identifier for this spawn point
 * @position: (transfer none): World position
 * @spawn_type: Type of spawn point
 *
 * Creates a new spawn point from a vector position.
 *
 * Returns: (transfer full): A newly allocated #LrgSpawnPoint3D
 */
LrgSpawnPoint3D *
lrg_spawn_point3d_new_from_vector (const gchar      *id,
                                   const GrlVector3 *position,
                                   LrgSpawnType      spawn_type)
{
    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (position != NULL, NULL);

    return lrg_spawn_point3d_new (id, position->x, position->y, position->z, spawn_type);
}

/**
 * lrg_spawn_point3d_copy:
 * @self: (nullable): A #LrgSpawnPoint3D
 *
 * Creates a copy of the spawn point.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LrgSpawnPoint3D *
lrg_spawn_point3d_copy (const LrgSpawnPoint3D *self)
{
    LrgSpawnPoint3D *copy;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    if (self == NULL)
        return NULL;

    copy = lrg_spawn_point3d_new (self->id,
                                  self->position.x,
                                  self->position.y,
                                  self->position.z,
                                  self->spawn_type);
    copy->rotation = self->rotation;

    if (self->entity_type != NULL)
        copy->entity_type = g_strdup (self->entity_type);

    /* Copy properties */
    g_hash_table_iter_init (&iter, self->properties);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GValue *new_value = g_new0 (GValue, 1);
        g_value_init (new_value, G_VALUE_TYPE ((GValue *)value));
        g_value_copy ((GValue *)value, new_value);
        g_hash_table_insert (copy->properties, g_strdup ((gchar *)key), new_value);
    }

    return copy;
}

/**
 * lrg_spawn_point3d_free:
 * @self: (nullable): A #LrgSpawnPoint3D
 *
 * Frees a spawn point.
 */
void
lrg_spawn_point3d_free (LrgSpawnPoint3D *self)
{
    if (self == NULL)
        return;

    g_clear_pointer (&self->id, g_free);
    g_clear_pointer (&self->entity_type, g_free);
    g_clear_pointer (&self->properties, g_hash_table_unref);
    g_free (self);
}

/**
 * lrg_spawn_point3d_get_id:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
const gchar *
lrg_spawn_point3d_get_id (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->id;
}

/**
 * lrg_spawn_point3d_get_position:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the world position.
 *
 * Returns: (transfer full): The position
 */
GrlVector3 *
lrg_spawn_point3d_get_position (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->position.x, self->position.y, self->position.z);
}

/**
 * lrg_spawn_point3d_get_rotation:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the rotation (euler angles in degrees).
 *
 * Returns: (transfer full): The rotation
 */
GrlVector3 *
lrg_spawn_point3d_get_rotation (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return grl_vector3_new (self->rotation.x, self->rotation.y, self->rotation.z);
}

/**
 * lrg_spawn_point3d_set_rotation:
 * @self: A #LrgSpawnPoint3D
 * @rotation: (transfer none): New rotation (euler angles in degrees)
 *
 * Sets the rotation.
 */
void
lrg_spawn_point3d_set_rotation (LrgSpawnPoint3D  *self,
                                const GrlVector3 *rotation)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (rotation != NULL);

    self->rotation.x = rotation->x;
    self->rotation.y = rotation->y;
    self->rotation.z = rotation->z;
}

/**
 * lrg_spawn_point3d_get_spawn_type:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the spawn type.
 *
 * Returns: The spawn type
 */
LrgSpawnType
lrg_spawn_point3d_get_spawn_type (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, LRG_SPAWN_TYPE_GENERIC);

    return self->spawn_type;
}

/**
 * lrg_spawn_point3d_get_entity_type:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the entity type name to spawn.
 *
 * Returns: (transfer none) (nullable): The entity type name, or %NULL if not set
 */
const gchar *
lrg_spawn_point3d_get_entity_type (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->entity_type;
}

/**
 * lrg_spawn_point3d_set_entity_type:
 * @self: A #LrgSpawnPoint3D
 * @entity_type: (nullable): Entity type name to spawn
 *
 * Sets the entity type to spawn at this point.
 */
void
lrg_spawn_point3d_set_entity_type (LrgSpawnPoint3D *self,
                                   const gchar     *entity_type)
{
    g_return_if_fail (self != NULL);

    g_free (self->entity_type);
    self->entity_type = g_strdup (entity_type);
}

/**
 * lrg_spawn_point3d_set_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 * @value: (transfer none): Property value
 *
 * Sets a custom property on the spawn point.
 * These properties can be used to configure spawned entities.
 */
void
lrg_spawn_point3d_set_property (LrgSpawnPoint3D *self,
                                const gchar     *key,
                                const GValue    *value)
{
    GValue *new_value;

    g_return_if_fail (self != NULL);
    g_return_if_fail (key != NULL);
    g_return_if_fail (value != NULL);

    new_value = g_new0 (GValue, 1);
    g_value_init (new_value, G_VALUE_TYPE (value));
    g_value_copy (value, new_value);

    g_hash_table_insert (self->properties, g_strdup (key), new_value);
}

/**
 * lrg_spawn_point3d_get_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 *
 * Gets a custom property from the spawn point.
 *
 * Returns: (transfer none) (nullable): The property value, or %NULL if not found
 */
const GValue *
lrg_spawn_point3d_get_property (const LrgSpawnPoint3D *self,
                                const gchar           *key)
{
    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (key != NULL, NULL);

    return g_hash_table_lookup (self->properties, key);
}

/**
 * lrg_spawn_point3d_has_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 *
 * Checks if a property is set.
 *
 * Returns: %TRUE if the property exists
 */
gboolean
lrg_spawn_point3d_has_property (const LrgSpawnPoint3D *self,
                                const gchar           *key)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->properties, key);
}

/**
 * lrg_spawn_point3d_get_property_keys:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets all property keys.
 *
 * Returns: (transfer container) (element-type utf8): List of property keys
 */
GList *
lrg_spawn_point3d_get_property_keys (const LrgSpawnPoint3D *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return g_hash_table_get_keys (self->properties);
}
