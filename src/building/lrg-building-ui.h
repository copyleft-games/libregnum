/* lrg-building-ui.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgBuildingUI - Build mode user interface.
 *
 * Provides a UI container for selecting buildings to place.
 * Displays building icons in a grid, supports category filtering,
 * and integrates with the placement system.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../ui/lrg-container.h"
#include "lrg-building-def.h"
#include "lrg-placement-system.h"

G_BEGIN_DECLS

#define LRG_TYPE_BUILDING_UI (lrg_building_ui_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgBuildingUI, lrg_building_ui, LRG, BUILDING_UI, LrgContainer)

/* Signals */

/**
 * LrgBuildingUI::building-selected:
 * @ui: the #LrgBuildingUI
 * @definition: the selected #LrgBuildingDef
 *
 * Emitted when a building is selected from the UI.
 *
 * Since: 1.0
 */

/**
 * LrgBuildingUI::demolish-selected:
 * @ui: the #LrgBuildingUI
 *
 * Emitted when the demolish button is clicked.
 *
 * Since: 1.0
 */

/**
 * LrgBuildingUI::category-changed:
 * @ui: the #LrgBuildingUI
 * @category: the new category filter
 *
 * Emitted when the category filter changes.
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_building_ui_new:
 *
 * Creates a new building UI.
 *
 * Returns: (transfer full): A new #LrgBuildingUI
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingUI *
lrg_building_ui_new (void);

/* Placement system integration */

/**
 * lrg_building_ui_get_placement_system:
 * @self: an #LrgBuildingUI
 *
 * Gets the placement system.
 *
 * Returns: (transfer none) (nullable): The placement system
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlacementSystem *
lrg_building_ui_get_placement_system (LrgBuildingUI *self);

/**
 * lrg_building_ui_set_placement_system:
 * @self: an #LrgBuildingUI
 * @system: (nullable): the placement system
 *
 * Sets the placement system to integrate with.
 * When set, selecting a building will automatically start placement.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_set_placement_system (LrgBuildingUI      *self,
                                      LrgPlacementSystem *system);

/* Building registration */

/**
 * lrg_building_ui_register:
 * @self: an #LrgBuildingUI
 * @definition: building definition to register
 *
 * Registers a building definition to appear in the UI.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_register (LrgBuildingUI  *self,
                          LrgBuildingDef *definition);

/**
 * lrg_building_ui_unregister:
 * @self: an #LrgBuildingUI
 * @id: building ID to unregister
 *
 * Removes a building from the UI.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_unregister (LrgBuildingUI *self,
                            const gchar   *id);

/**
 * lrg_building_ui_get_building:
 * @self: an #LrgBuildingUI
 * @id: building ID
 *
 * Gets a registered building by ID.
 *
 * Returns: (transfer none) (nullable): The building definition
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingDef *
lrg_building_ui_get_building (LrgBuildingUI *self,
                              const gchar   *id);

/**
 * lrg_building_ui_get_all_buildings:
 * @self: an #LrgBuildingUI
 *
 * Gets all registered building definitions.
 *
 * Returns: (transfer container) (element-type LrgBuildingDef): List of buildings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_building_ui_get_all_buildings (LrgBuildingUI *self);

/**
 * lrg_building_ui_clear_buildings:
 * @self: an #LrgBuildingUI
 *
 * Removes all registered buildings.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_clear_buildings (LrgBuildingUI *self);

/* Category filtering */

/**
 * lrg_building_ui_get_category_filter:
 * @self: an #LrgBuildingUI
 *
 * Gets the current category filter.
 * -1 means all categories are shown.
 *
 * Returns: The category filter, or -1 for all
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_ui_get_category_filter (LrgBuildingUI *self);

/**
 * lrg_building_ui_set_category_filter:
 * @self: an #LrgBuildingUI
 * @category: category to filter, or -1 for all
 *
 * Filters buildings by category.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_set_category_filter (LrgBuildingUI *self,
                                     gint           category);

/**
 * lrg_building_ui_get_buildings_by_category:
 * @self: an #LrgBuildingUI
 * @category: category to filter
 *
 * Gets all buildings in a specific category.
 *
 * Returns: (transfer container) (element-type LrgBuildingDef): List of buildings
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *
lrg_building_ui_get_buildings_by_category (LrgBuildingUI       *self,
                                           LrgBuildingCategory  category);

/* Selection */

/**
 * lrg_building_ui_get_selected:
 * @self: an #LrgBuildingUI
 *
 * Gets the currently selected building.
 *
 * Returns: (transfer none) (nullable): The selected building, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingDef *
lrg_building_ui_get_selected (LrgBuildingUI *self);

/**
 * lrg_building_ui_select:
 * @self: an #LrgBuildingUI
 * @id: (nullable): building ID to select, or %NULL to deselect
 *
 * Selects a building by ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_select (LrgBuildingUI *self,
                        const gchar   *id);

/**
 * lrg_building_ui_deselect:
 * @self: an #LrgBuildingUI
 *
 * Deselects the current building.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_deselect (LrgBuildingUI *self);

/* Demolish button */

/**
 * lrg_building_ui_get_show_demolish:
 * @self: an #LrgBuildingUI
 *
 * Gets whether the demolish button is shown.
 *
 * Returns: %TRUE if demolish button is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_building_ui_get_show_demolish (LrgBuildingUI *self);

/**
 * lrg_building_ui_set_show_demolish:
 * @self: an #LrgBuildingUI
 * @show: whether to show demolish button
 *
 * Sets whether to show the demolish button.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_set_show_demolish (LrgBuildingUI *self,
                                   gboolean       show);

/* Layout options */

/**
 * lrg_building_ui_get_columns:
 * @self: an #LrgBuildingUI
 *
 * Gets the number of columns in the grid.
 *
 * Returns: Column count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint
lrg_building_ui_get_columns (LrgBuildingUI *self);

/**
 * lrg_building_ui_set_columns:
 * @self: an #LrgBuildingUI
 * @columns: number of columns
 *
 * Sets the number of columns for the building grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_set_columns (LrgBuildingUI *self,
                             gint           columns);

/**
 * lrg_building_ui_get_button_size:
 * @self: an #LrgBuildingUI
 *
 * Gets the size of building buttons.
 *
 * Returns: Button size in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat
lrg_building_ui_get_button_size (LrgBuildingUI *self);

/**
 * lrg_building_ui_set_button_size:
 * @self: an #LrgBuildingUI
 * @size: button size in pixels
 *
 * Sets the size of building buttons.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_set_button_size (LrgBuildingUI *self,
                                 gfloat         size);

/* UI rebuild */

/**
 * lrg_building_ui_rebuild:
 * @self: an #LrgBuildingUI
 *
 * Rebuilds the UI widgets based on registered buildings
 * and current category filter.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_building_ui_rebuild (LrgBuildingUI *self);

G_END_DECLS
