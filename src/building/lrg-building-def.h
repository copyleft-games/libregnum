/* lrg-building-def.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBuildingDef - Building template/definition for city builders.
 *
 * Defines the properties, costs, and behavior of a building type.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_BUILDING_DEF (lrg_building_def_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgBuildingDef, lrg_building_def, LRG, BUILDING_DEF, GObject)

/* Note: LrgBuildingCategory, LrgTerrainType, and LrgRotation are defined in lrg-enums.h */

/**
 * LrgBuildCost:
 * @resources: Hash table of resource IDs to amounts
 *
 * A boxed type representing resource costs for building.
 */
typedef struct _LrgBuildCost LrgBuildCost;

struct _LrgBuildCost
{
    GHashTable *costs;  /* resource_id -> gdouble */
};

/**
 * LrgBuildingDefClass:
 * @parent_class: Parent class
 * @can_build: Check if building can be placed at location
 * @on_built: Called when building is placed
 * @on_destroyed: Called when building is removed
 *
 * Virtual table for #LrgBuildingDef.
 *
 * Since: 1.0
 */
struct _LrgBuildingDefClass
{
    GObjectClass parent_class;

    /*< public >*/

    /**
     * LrgBuildingDefClass::can_build:
     * @self: Building definition
     * @grid_x: Grid X position
     * @grid_y: Grid Y position
     * @terrain: Terrain flags at location
     *
     * Checks if building can be placed at the given location.
     *
     * Returns: %TRUE if placement is valid
     */
    gboolean (*can_build)     (LrgBuildingDef *self,
                               gint            grid_x,
                               gint            grid_y,
                               LrgTerrainType terrain);

    /**
     * LrgBuildingDefClass::on_built:
     * @self: Building definition
     * @instance: The placed building instance
     *
     * Called when a building of this type is placed.
     */
    void     (*on_built)      (LrgBuildingDef *self,
                               gpointer        instance);

    /**
     * LrgBuildingDefClass::on_destroyed:
     * @self: Building definition
     * @instance: The removed building instance
     *
     * Called when a building of this type is removed.
     */
    void     (*on_destroyed)  (LrgBuildingDef *self,
                               gpointer        instance);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LrgBuildCost:
 *
 * A boxed type representing resource costs for building.
 */
#define LRG_TYPE_BUILD_COST (lrg_build_cost_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_build_cost_get_type (void) G_GNUC_CONST;

/**
 * lrg_build_cost_new:
 *
 * Creates a new empty build cost.
 *
 * Returns: (transfer full): A new #LrgBuildCost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildCost *
lrg_build_cost_new (void);

/**
 * lrg_build_cost_copy:
 * @self: an #LrgBuildCost
 *
 * Creates a copy.
 *
 * Returns: (transfer full): A copy
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildCost *
lrg_build_cost_copy (const LrgBuildCost *self);

/**
 * lrg_build_cost_free:
 * @self: an #LrgBuildCost
 *
 * Frees the cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_cost_free (LrgBuildCost *self);

/**
 * lrg_build_cost_set:
 * @self: an #LrgBuildCost
 * @resource_id: Resource identifier
 * @amount: Amount required
 *
 * Sets a resource cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_build_cost_set (LrgBuildCost *self,
                    const gchar  *resource_id,
                    gdouble       amount);

/**
 * lrg_build_cost_get:
 * @self: an #LrgBuildCost
 * @resource_id: Resource identifier
 *
 * Gets a resource cost.
 *
 * Returns: Amount required, or 0 if not set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_build_cost_get (const LrgBuildCost *self,
                    const gchar        *resource_id);

/**
 * lrg_build_cost_get_resources:
 * @self: an #LrgBuildCost
 *
 * Gets all resource IDs in this cost.
 *
 * Returns: (transfer container) (element-type utf8): Array of resource IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_build_cost_get_resources (const LrgBuildCost *self);

/**
 * lrg_build_cost_is_empty:
 * @self: an #LrgBuildCost
 *
 * Checks if the cost has any requirements.
 *
 * Returns: %TRUE if empty
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_build_cost_is_empty (const LrgBuildCost *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgBuildCost, lrg_build_cost_free)

/* Construction */

/**
 * lrg_building_def_new:
 * @id: Unique identifier
 *
 * Creates a new building definition.
 *
 * Returns: (transfer full): A new #LrgBuildingDef
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingDef *
lrg_building_def_new (const gchar *id);

/* Identification */

/**
 * lrg_building_def_get_id:
 * @self: an #LrgBuildingDef
 *
 * Gets the building ID.
 *
 * Returns: (transfer none): The ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_building_def_get_id (LrgBuildingDef *self);

/**
 * lrg_building_def_get_name:
 * @self: an #LrgBuildingDef
 *
 * Gets the display name.
 *
 * Returns: (transfer none) (nullable): The name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_building_def_get_name (LrgBuildingDef *self);

/**
 * lrg_building_def_set_name:
 * @self: an #LrgBuildingDef
 * @name: (nullable): Display name
 *
 * Sets the display name.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_name (LrgBuildingDef *self,
                           const gchar    *name);

/**
 * lrg_building_def_get_description:
 * @self: an #LrgBuildingDef
 *
 * Gets the description.
 *
 * Returns: (transfer none) (nullable): The description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_building_def_get_description (LrgBuildingDef *self);

/**
 * lrg_building_def_set_description:
 * @self: an #LrgBuildingDef
 * @description: (nullable): Description
 *
 * Sets the description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_description (LrgBuildingDef *self,
                                  const gchar    *description);

/**
 * lrg_building_def_get_icon:
 * @self: an #LrgBuildingDef
 *
 * Gets the icon path.
 *
 * Returns: (transfer none) (nullable): The icon path
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_building_def_get_icon (LrgBuildingDef *self);

/**
 * lrg_building_def_set_icon:
 * @self: an #LrgBuildingDef
 * @icon: (nullable): Icon path
 *
 * Sets the icon path.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_icon (LrgBuildingDef *self,
                           const gchar    *icon);

/* Dimensions */

/**
 * lrg_building_def_get_width:
 * @self: an #LrgBuildingDef
 *
 * Gets the width in grid cells.
 *
 * Returns: Width
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_def_get_width (LrgBuildingDef *self);

/**
 * lrg_building_def_set_width:
 * @self: an #LrgBuildingDef
 * @width: Width in cells
 *
 * Sets the width.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_width (LrgBuildingDef *self,
                            gint            width);

/**
 * lrg_building_def_get_height:
 * @self: an #LrgBuildingDef
 *
 * Gets the height in grid cells.
 *
 * Returns: Height
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_def_get_height (LrgBuildingDef *self);

/**
 * lrg_building_def_set_height:
 * @self: an #LrgBuildingDef
 * @height: Height in cells
 *
 * Sets the height.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_height (LrgBuildingDef *self,
                             gint            height);

/**
 * lrg_building_def_set_size:
 * @self: an #LrgBuildingDef
 * @width: Width in cells
 * @height: Height in cells
 *
 * Sets both dimensions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_size (LrgBuildingDef *self,
                           gint            width,
                           gint            height);

/* Category and terrain */

/**
 * lrg_building_def_get_category:
 * @self: an #LrgBuildingDef
 *
 * Gets the building category.
 *
 * Returns: Category
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingCategory
lrg_building_def_get_category (LrgBuildingDef *self);

/**
 * lrg_building_def_set_category:
 * @self: an #LrgBuildingDef
 * @category: Category
 *
 * Sets the building category.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_category (LrgBuildingDef     *self,
                               LrgBuildingCategory category);

/**
 * lrg_building_def_get_buildable_on:
 * @self: an #LrgBuildingDef
 *
 * Gets terrain flags where building can be placed.
 *
 * Returns: Terrain flags
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTerrainType
lrg_building_def_get_buildable_on (LrgBuildingDef *self);

/**
 * lrg_building_def_set_buildable_on:
 * @self: an #LrgBuildingDef
 * @terrain: Terrain flags
 *
 * Sets terrain types where building can be placed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_buildable_on (LrgBuildingDef  *self,
                                   LrgTerrainType  terrain);

/* Levels */

/**
 * lrg_building_def_get_max_level:
 * @self: an #LrgBuildingDef
 *
 * Gets maximum upgrade level.
 *
 * Returns: Max level (1 = no upgrades)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_def_get_max_level (LrgBuildingDef *self);

/**
 * lrg_building_def_set_max_level:
 * @self: an #LrgBuildingDef
 * @max_level: Maximum level
 *
 * Sets maximum upgrade level.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_max_level (LrgBuildingDef *self,
                                gint            max_level);

/* Costs */

/**
 * lrg_building_def_get_cost:
 * @self: an #LrgBuildingDef
 *
 * Gets the initial build cost.
 *
 * Returns: (transfer none): The build cost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBuildCost *
lrg_building_def_get_cost (LrgBuildingDef *self);

/**
 * lrg_building_def_set_cost:
 * @self: an #LrgBuildingDef
 * @cost: Build cost
 *
 * Sets the initial build cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_cost (LrgBuildingDef     *self,
                           const LrgBuildCost *cost);

/**
 * lrg_building_def_set_cost_simple:
 * @self: an #LrgBuildingDef
 * @resource_id: Resource ID
 * @amount: Amount required
 *
 * Sets a simple single-resource cost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_cost_simple (LrgBuildingDef *self,
                                  const gchar    *resource_id,
                                  gdouble         amount);

/**
 * lrg_building_def_get_upgrade_cost:
 * @self: an #LrgBuildingDef
 * @level: Target level
 *
 * Gets the cost to upgrade to a specific level.
 *
 * Returns: (transfer none) (nullable): The upgrade cost, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const LrgBuildCost *
lrg_building_def_get_upgrade_cost (LrgBuildingDef *self,
                                   gint            level);

/**
 * lrg_building_def_set_upgrade_cost:
 * @self: an #LrgBuildingDef
 * @level: Target level
 * @cost: Upgrade cost
 *
 * Sets the cost to upgrade to a specific level.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_upgrade_cost (LrgBuildingDef     *self,
                                   gint                level,
                                   const LrgBuildCost *cost);

/* Demolition */

/**
 * lrg_building_def_get_refund_percent:
 * @self: an #LrgBuildingDef
 *
 * Gets the refund percentage when demolished.
 *
 * Returns: Refund percentage (0.0 - 1.0)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_building_def_get_refund_percent (LrgBuildingDef *self);

/**
 * lrg_building_def_set_refund_percent:
 * @self: an #LrgBuildingDef
 * @percent: Refund percentage (0.0 - 1.0)
 *
 * Sets the refund percentage when demolished.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_set_refund_percent (LrgBuildingDef *self,
                                     gdouble         percent);

/* Virtual method wrappers */

/**
 * lrg_building_def_can_build:
 * @self: an #LrgBuildingDef
 * @grid_x: Grid X position
 * @grid_y: Grid Y position
 * @terrain: Terrain at location
 *
 * Checks if building can be placed.
 *
 * Returns: %TRUE if valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_def_can_build (LrgBuildingDef *self,
                            gint            grid_x,
                            gint            grid_y,
                            LrgTerrainType terrain);

/**
 * lrg_building_def_on_built:
 * @self: an #LrgBuildingDef
 * @instance: Building instance
 *
 * Called when a building is placed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_on_built (LrgBuildingDef *self,
                           gpointer        instance);

/**
 * lrg_building_def_on_destroyed:
 * @self: an #LrgBuildingDef
 * @instance: Building instance
 *
 * Called when a building is removed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_def_on_destroyed (LrgBuildingDef *self,
                               gpointer        instance);

G_END_DECLS
