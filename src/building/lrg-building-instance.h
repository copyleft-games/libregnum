/* lrg-building-instance.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBuildingInstance - Placed building in the world.
 *
 * Represents an actual building placed on the grid with position,
 * rotation, level, and runtime state.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-building-def.h"

G_BEGIN_DECLS

#define LRG_TYPE_BUILDING_INSTANCE (lrg_building_instance_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgBuildingInstance, lrg_building_instance, LRG, BUILDING_INSTANCE, GObject)

/**
 * LrgBuildingInstanceClass:
 * @parent_class: Parent class
 * @on_placed: Called when building is placed on grid
 * @on_removed: Called when building is removed from grid
 * @on_upgraded: Called when building is upgraded
 * @on_damaged: Called when building takes damage
 *
 * Virtual table for #LrgBuildingInstance.
 *
 * Since: 1.0
 */
struct _LrgBuildingInstanceClass
{
    GObjectClass parent_class;

    /*< public >*/

    void (*on_placed)   (LrgBuildingInstance *self);
    void (*on_removed)  (LrgBuildingInstance *self);
    void (*on_upgraded) (LrgBuildingInstance *self,
                         gint                 new_level);
    void (*on_damaged)  (LrgBuildingInstance *self,
                         gdouble              damage);

    /*< private >*/
    gpointer _reserved[8];
};

/* Construction */

/**
 * lrg_building_instance_new:
 * @definition: Building definition
 * @grid_x: Grid X position
 * @grid_y: Grid Y position
 *
 * Creates a new building instance.
 *
 * Returns: (transfer full): A new #LrgBuildingInstance
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_building_instance_new (LrgBuildingDef *definition,
                           gint            grid_x,
                           gint            grid_y);

/* Definition */

/**
 * lrg_building_instance_get_definition:
 * @self: an #LrgBuildingInstance
 *
 * Gets the building definition.
 *
 * Returns: (transfer none): The definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingDef *
lrg_building_instance_get_definition (LrgBuildingInstance *self);

/* Position */

/**
 * lrg_building_instance_get_grid_x:
 * @self: an #LrgBuildingInstance
 *
 * Gets the grid X position.
 *
 * Returns: Grid X
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_instance_get_grid_x (LrgBuildingInstance *self);

/**
 * lrg_building_instance_get_grid_y:
 * @self: an #LrgBuildingInstance
 *
 * Gets the grid Y position.
 *
 * Returns: Grid Y
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_instance_get_grid_y (LrgBuildingInstance *self);

/**
 * lrg_building_instance_set_position:
 * @self: an #LrgBuildingInstance
 * @grid_x: New grid X
 * @grid_y: New grid Y
 *
 * Sets the grid position (for moving buildings).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_set_position (LrgBuildingInstance *self,
                                    gint                 grid_x,
                                    gint                 grid_y);

/* Rotation */

/**
 * lrg_building_instance_get_rotation:
 * @self: an #LrgBuildingInstance
 *
 * Gets the rotation.
 *
 * Returns: Rotation value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRotation
lrg_building_instance_get_rotation (LrgBuildingInstance *self);

/**
 * lrg_building_instance_set_rotation:
 * @self: an #LrgBuildingInstance
 * @rotation: New rotation
 *
 * Sets the rotation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_set_rotation (LrgBuildingInstance *self,
                                    LrgRotation          rotation);

/**
 * lrg_building_instance_rotate_cw:
 * @self: an #LrgBuildingInstance
 *
 * Rotates 90 degrees clockwise.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_rotate_cw (LrgBuildingInstance *self);

/**
 * lrg_building_instance_rotate_ccw:
 * @self: an #LrgBuildingInstance
 *
 * Rotates 90 degrees counter-clockwise.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_rotate_ccw (LrgBuildingInstance *self);

/**
 * lrg_building_instance_get_effective_width:
 * @self: an #LrgBuildingInstance
 *
 * Gets width accounting for rotation.
 *
 * Returns: Effective width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_instance_get_effective_width (LrgBuildingInstance *self);

/**
 * lrg_building_instance_get_effective_height:
 * @self: an #LrgBuildingInstance
 *
 * Gets height accounting for rotation.
 *
 * Returns: Effective height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_instance_get_effective_height (LrgBuildingInstance *self);

/* Level */

/**
 * lrg_building_instance_get_level:
 * @self: an #LrgBuildingInstance
 *
 * Gets the current level.
 *
 * Returns: Current level
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_instance_get_level (LrgBuildingInstance *self);

/**
 * lrg_building_instance_can_upgrade:
 * @self: an #LrgBuildingInstance
 *
 * Checks if building can be upgraded.
 *
 * Returns: %TRUE if upgradeable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_can_upgrade (LrgBuildingInstance *self);

/**
 * lrg_building_instance_upgrade:
 * @self: an #LrgBuildingInstance
 *
 * Upgrades the building by one level.
 * Does NOT check or deduct costs.
 *
 * Returns: %TRUE if upgraded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_upgrade (LrgBuildingInstance *self);

/* Health */

/**
 * lrg_building_instance_get_health:
 * @self: an #LrgBuildingInstance
 *
 * Gets current health.
 *
 * Returns: Current health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_building_instance_get_health (LrgBuildingInstance *self);

/**
 * lrg_building_instance_get_max_health:
 * @self: an #LrgBuildingInstance
 *
 * Gets maximum health.
 *
 * Returns: Maximum health
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_building_instance_get_max_health (LrgBuildingInstance *self);

/**
 * lrg_building_instance_set_max_health:
 * @self: an #LrgBuildingInstance
 * @max_health: Maximum health
 *
 * Sets maximum health.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_set_max_health (LrgBuildingInstance *self,
                                      gdouble              max_health);

/**
 * lrg_building_instance_damage:
 * @self: an #LrgBuildingInstance
 * @amount: Damage amount
 *
 * Applies damage to the building.
 *
 * Returns: %TRUE if building was destroyed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_damage (LrgBuildingInstance *self,
                              gdouble              amount);

/**
 * lrg_building_instance_repair:
 * @self: an #LrgBuildingInstance
 * @amount: Repair amount
 *
 * Repairs the building.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_repair (LrgBuildingInstance *self,
                              gdouble              amount);

/**
 * lrg_building_instance_is_destroyed:
 * @self: an #LrgBuildingInstance
 *
 * Checks if building is destroyed.
 *
 * Returns: %TRUE if destroyed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_is_destroyed (LrgBuildingInstance *self);

/* State */

/**
 * lrg_building_instance_is_active:
 * @self: an #LrgBuildingInstance
 *
 * Checks if building is active (producing, etc.).
 *
 * Returns: %TRUE if active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_is_active (LrgBuildingInstance *self);

/**
 * lrg_building_instance_set_active:
 * @self: an #LrgBuildingInstance
 * @active: Whether to activate
 *
 * Sets whether building is active.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_set_active (LrgBuildingInstance *self,
                                  gboolean             active);

/* User data */

/**
 * lrg_building_instance_get_data:
 * @self: an #LrgBuildingInstance
 * @key: Data key
 *
 * Gets custom data.
 *
 * Returns: (transfer none) (nullable): Data value
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gpointer
lrg_building_instance_get_data (LrgBuildingInstance *self,
                                const gchar         *key);

/**
 * lrg_building_instance_set_data:
 * @self: an #LrgBuildingInstance
 * @key: Data key
 * @data: (nullable): Data value
 * @destroy: (nullable): Destroy function
 *
 * Sets custom data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_instance_set_data (LrgBuildingInstance *self,
                                const gchar         *key,
                                gpointer             data,
                                GDestroyNotify       destroy);

/* Bounds checking */

/**
 * lrg_building_instance_contains_cell:
 * @self: an #LrgBuildingInstance
 * @cell_x: Cell X to check
 * @cell_y: Cell Y to check
 *
 * Checks if building occupies the given cell.
 *
 * Returns: %TRUE if building occupies cell
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_instance_contains_cell (LrgBuildingInstance *self,
                                     gint                 cell_x,
                                     gint                 cell_y);

G_END_DECLS
