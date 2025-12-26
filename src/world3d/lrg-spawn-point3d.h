/* lrg-spawn-point3d.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Spawn point type for 3D levels.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_SPAWN_POINT3D (lrg_spawn_point3d_get_type ())

/**
 * LrgSpawnPoint3D:
 *
 * A spawn point in a 3D level.
 *
 * Spawn points define locations where entities (players, enemies, NPCs, items)
 * can be created in the game world.
 */
typedef struct _LrgSpawnPoint3D LrgSpawnPoint3D;

LRG_AVAILABLE_IN_ALL
GType               lrg_spawn_point3d_get_type          (void) G_GNUC_CONST;

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
LRG_AVAILABLE_IN_ALL
LrgSpawnPoint3D *   lrg_spawn_point3d_new               (const gchar        *id,
                                                         gfloat              x,
                                                         gfloat              y,
                                                         gfloat              z,
                                                         LrgSpawnType        spawn_type);

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
LRG_AVAILABLE_IN_ALL
LrgSpawnPoint3D *   lrg_spawn_point3d_new_from_vector   (const gchar        *id,
                                                         const GrlVector3   *position,
                                                         LrgSpawnType        spawn_type);

/**
 * lrg_spawn_point3d_copy:
 * @self: (nullable): A #LrgSpawnPoint3D
 *
 * Creates a copy of the spawn point.
 *
 * Returns: (transfer full) (nullable): A copy, or %NULL if @self is %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnPoint3D *   lrg_spawn_point3d_copy              (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_free:
 * @self: (nullable): A #LrgSpawnPoint3D
 *
 * Frees a spawn point.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_spawn_point3d_free              (LrgSpawnPoint3D    *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgSpawnPoint3D, lrg_spawn_point3d_free)

/**
 * lrg_spawn_point3d_get_id:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the unique identifier.
 *
 * Returns: (transfer none): The identifier
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_spawn_point3d_get_id            (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_get_position:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the world position.
 *
 * Returns: (transfer full): The position
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_spawn_point3d_get_position      (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_get_rotation:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the rotation (euler angles in degrees).
 *
 * Returns: (transfer full): The rotation
 */
LRG_AVAILABLE_IN_ALL
GrlVector3 *        lrg_spawn_point3d_get_rotation      (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_set_rotation:
 * @self: A #LrgSpawnPoint3D
 * @rotation: (transfer none): New rotation (euler angles in degrees)
 *
 * Sets the rotation.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_spawn_point3d_set_rotation      (LrgSpawnPoint3D    *self,
                                                         const GrlVector3   *rotation);

/**
 * lrg_spawn_point3d_get_spawn_type:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the spawn type.
 *
 * Returns: The spawn type
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnType        lrg_spawn_point3d_get_spawn_type    (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_get_entity_type:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets the entity type name to spawn.
 *
 * Returns: (transfer none) (nullable): The entity type name, or %NULL if not set
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_spawn_point3d_get_entity_type   (const LrgSpawnPoint3D *self);

/**
 * lrg_spawn_point3d_set_entity_type:
 * @self: A #LrgSpawnPoint3D
 * @entity_type: (nullable): Entity type name to spawn
 *
 * Sets the entity type to spawn at this point.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_spawn_point3d_set_entity_type   (LrgSpawnPoint3D    *self,
                                                         const gchar        *entity_type);

/**
 * lrg_spawn_point3d_set_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 * @value: (transfer none): Property value
 *
 * Sets a custom property on the spawn point.
 * These properties can be used to configure spawned entities.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_spawn_point3d_set_property      (LrgSpawnPoint3D    *self,
                                                         const gchar        *key,
                                                         const GValue       *value);

/**
 * lrg_spawn_point3d_get_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 *
 * Gets a custom property from the spawn point.
 *
 * Returns: (transfer none) (nullable): The property value, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
const GValue *      lrg_spawn_point3d_get_property      (const LrgSpawnPoint3D *self,
                                                         const gchar        *key);

/**
 * lrg_spawn_point3d_has_property:
 * @self: A #LrgSpawnPoint3D
 * @key: Property key
 *
 * Checks if a property is set.
 *
 * Returns: %TRUE if the property exists
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_spawn_point3d_has_property      (const LrgSpawnPoint3D *self,
                                                         const gchar        *key);

/**
 * lrg_spawn_point3d_get_property_keys:
 * @self: A #LrgSpawnPoint3D
 *
 * Gets all property keys.
 *
 * Returns: (transfer container) (element-type utf8): List of property keys
 */
LRG_AVAILABLE_IN_ALL
GList *             lrg_spawn_point3d_get_property_keys (const LrgSpawnPoint3D *self);

G_END_DECLS
