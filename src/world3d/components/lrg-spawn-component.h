/* lrg-spawn-component.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgSpawnComponent: marks its entity as a spawn point (players, enemies,
 * items...) with a type, a team and tags.  Games find spawn points with
 * lrg_spawn_component_find() and read the entity's world transform.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../../lrg-version.h"
#include "../../lrg-types.h"
#include "../../lrg-enums.h"
#include "../../ecs/lrg-component.h"
#include "../lrg-world3d.h"
#include "../lrg-spawn-point3d.h"

G_BEGIN_DECLS

/**
 * LRG_SPAWN_COMPONENT_ANY_TEAM:
 *
 * Team wildcard for lrg_spawn_component_find().
 */
#define LRG_SPAWN_COMPONENT_ANY_TEAM (-1)

#define LRG_TYPE_SPAWN_COMPONENT (lrg_spawn_component_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSpawnComponent, lrg_spawn_component, LRG, SPAWN_COMPONENT, LrgComponent)

/**
 * lrg_spawn_component_new:
 *
 * Creates a %LRG_SPAWN_TYPE_GENERIC spawn point on team 0 without tags.
 *
 * Returns: (transfer full): a new #LrgSpawnComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnComponent * lrg_spawn_component_new (void);

/**
 * lrg_spawn_component_new_with_type:
 * @spawn_type: what spawns here
 * @team: the team number (0 for none)
 *
 * Returns: (transfer full): a new #LrgSpawnComponent
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnComponent * lrg_spawn_component_new_with_type (LrgSpawnType spawn_type,
                                                       gint         team);

/**
 * lrg_spawn_component_get_spawn_type:
 * @self: an #LrgSpawnComponent
 *
 * Returns: what spawns here
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnType lrg_spawn_component_get_spawn_type (LrgSpawnComponent *self);

/**
 * lrg_spawn_component_set_spawn_type:
 * @self: an #LrgSpawnComponent
 * @spawn_type: what spawns here
 *
 * Sets #LrgSpawnComponent:spawn-type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spawn_component_set_spawn_type (LrgSpawnComponent *self,
                                         LrgSpawnType       spawn_type);

/**
 * lrg_spawn_component_get_team:
 * @self: an #LrgSpawnComponent
 *
 * Returns: the team number
 */
LRG_AVAILABLE_IN_ALL
gint lrg_spawn_component_get_team (LrgSpawnComponent *self);

/**
 * lrg_spawn_component_set_team:
 * @self: an #LrgSpawnComponent
 * @team: the team number (>= 0)
 *
 * Sets #LrgSpawnComponent:team.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spawn_component_set_team (LrgSpawnComponent *self,
                                   gint               team);

/**
 * lrg_spawn_component_get_entity_type:
 * @self: an #LrgSpawnComponent
 *
 * Returns: (transfer none) (nullable): what to spawn (a prefab path, a
 *   type name: the game decides)
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_spawn_component_get_entity_type (LrgSpawnComponent *self);

/**
 * lrg_spawn_component_set_entity_type:
 * @self: an #LrgSpawnComponent
 * @entity_type: (nullable): what to spawn
 *
 * Sets #LrgSpawnComponent:entity-type.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spawn_component_set_entity_type (LrgSpawnComponent *self,
                                          const gchar       *entity_type);

/**
 * lrg_spawn_component_get_tags:
 * @self: an #LrgSpawnComponent
 *
 * Returns: (transfer none) (array zero-terminated=1): the tags (an empty
 *   array when there are none)
 */
LRG_AVAILABLE_IN_ALL
const gchar * const * lrg_spawn_component_get_tags (LrgSpawnComponent *self);

/**
 * lrg_spawn_component_set_tags:
 * @self: an #LrgSpawnComponent
 * @tags: (nullable) (array zero-terminated=1): the new tags
 *
 * Replaces the tags (duplicates are dropped).
 */
LRG_AVAILABLE_IN_ALL
void lrg_spawn_component_set_tags (LrgSpawnComponent   *self,
                                   const gchar * const *tags);

/**
 * lrg_spawn_component_add_tag:
 * @self: an #LrgSpawnComponent
 * @tag: a tag
 *
 * Adds @tag unless present.
 */
LRG_AVAILABLE_IN_ALL
void lrg_spawn_component_add_tag (LrgSpawnComponent *self,
                                  const gchar       *tag);

/**
 * lrg_spawn_component_has_tag:
 * @self: an #LrgSpawnComponent
 * @tag: a tag
 *
 * Returns: %TRUE if @tag is one of the spawn point's tags
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spawn_component_has_tag (LrgSpawnComponent *self,
                                      const gchar       *tag);

/**
 * lrg_spawn_component_matches:
 * @self: an #LrgSpawnComponent
 * @tag: (nullable): a required tag, %NULL for any
 * @team: a required team, or %LRG_SPAWN_COMPONENT_ANY_TEAM
 *
 * Returns: %TRUE if the component is enabled and matches both filters
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_spawn_component_matches (LrgSpawnComponent *self,
                                      const gchar       *tag,
                                      gint               team);

/**
 * lrg_spawn_component_to_spawn_point:
 * @self: an #LrgSpawnComponent
 *
 * Builds an #LrgSpawnPoint3D at the entity's world position, with its
 * world euler rotation (degrees), the spawn type, the entity type and
 * the team as the "team" property.  The id is the entity's name (or
 * GUID).
 *
 * Returns: (transfer full): a new spawn point
 */
LRG_AVAILABLE_IN_ALL
LrgSpawnPoint3D * lrg_spawn_component_to_spawn_point (LrgSpawnComponent *self);

/**
 * lrg_spawn_component_find:
 * @world: an #LrgWorld3D
 * @tag: (nullable): a required tag, %NULL for any
 * @team: a required team, or %LRG_SPAWN_COMPONENT_ANY_TEAM
 *
 * Lists the enabled spawn points of active entities in @world matching
 * the filters, in hierarchy order.
 *
 * Returns: (transfer container) (element-type LrgSpawnComponent): the
 *   matching components
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_spawn_component_find (LrgWorld3D  *world,
                                      const gchar *tag,
                                      gint         team);

G_END_DECLS
