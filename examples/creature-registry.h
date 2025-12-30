/* creature-registry.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Shared header for creature registry API.
 * Used by both the main game and DLC modules to register creature types.
 */

#ifndef CREATURE_REGISTRY_H
#define CREATURE_REGISTRY_H

#include <glib.h>
#include <gmodule.h>

G_BEGIN_DECLS

/**
 * CreatureType:
 * @name: Display name of the creature
 * @r: Red color component (0-255)
 * @g: Green color component (0-255)
 * @b: Blue color component (0-255)
 * @speed: Movement speed multiplier
 * @from_mod: TRUE if this creature was added by a mod
 *
 * Represents a type of creature that can be spawned in the game.
 */
typedef struct
{
    gchar  *name;
    guint8  r;
    guint8  g;
    guint8  b;
    gfloat  speed;
    gboolean from_mod;
} CreatureType;

/**
 * register_creature_type:
 * @name: Display name of the creature
 * @r: Red color component (0-255)
 * @g: Green color component (0-255)
 * @b: Blue color component (0-255)
 * @speed: Movement speed multiplier
 * @from_mod: TRUE if this creature is from a mod/DLC
 *
 * Registers a new creature type with the game.
 * This function is exported by the main game and can be called by DLC modules.
 */
G_MODULE_EXPORT void
register_creature_type (const gchar *name,
                        guint8       r,
                        guint8       g,
                        guint8       b,
                        gfloat       speed,
                        gboolean     from_mod);

/**
 * get_creature_types:
 *
 * Gets all registered creature types.
 *
 * Returns: (transfer none) (element-type CreatureType): Array of creature types
 */
G_MODULE_EXPORT GPtrArray *
get_creature_types (void);

G_END_DECLS

#endif /* CREATURE_REGISTRY_H */
