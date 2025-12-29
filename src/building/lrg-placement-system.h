/* lrg-placement-system.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPlacementSystem - Building placement workflow manager.
 *
 * Handles the entire building placement workflow including:
 * - Selection of building to place
 * - Ghost preview positioning
 * - Validation and resource checking
 * - Placement confirmation and demolition
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-building-def.h"
#include "lrg-building-instance.h"
#include "lrg-build-grid.h"

G_BEGIN_DECLS

/**
 * LrgPlacementState:
 * @LRG_PLACEMENT_STATE_IDLE: Not placing anything
 * @LRG_PLACEMENT_STATE_PLACING: Currently placing a building
 * @LRG_PLACEMENT_STATE_DEMOLISHING: In demolition mode
 *
 * States for the placement system.
 *
 * Since: 1.0
 */
typedef enum
{
    LRG_PLACEMENT_STATE_IDLE,
    LRG_PLACEMENT_STATE_PLACING,
    LRG_PLACEMENT_STATE_DEMOLISHING
} LrgPlacementState;

#define LRG_TYPE_PLACEMENT_SYSTEM (lrg_placement_system_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlacementSystem, lrg_placement_system, LRG, PLACEMENT_SYSTEM, GObject)

/* Signals */

/**
 * LrgPlacementSystem::placement-started:
 * @system: the #LrgPlacementSystem
 * @definition: the building definition being placed
 *
 * Emitted when placement mode is entered.
 *
 * Since: 1.0
 */

/**
 * LrgPlacementSystem::placement-cancelled:
 * @system: the #LrgPlacementSystem
 *
 * Emitted when placement is cancelled.
 *
 * Since: 1.0
 */

/**
 * LrgPlacementSystem::placement-confirmed:
 * @system: the #LrgPlacementSystem
 * @building: the newly placed building
 *
 * Emitted when a building is successfully placed.
 *
 * Since: 1.0
 */

/**
 * LrgPlacementSystem::building-demolished:
 * @system: the #LrgPlacementSystem
 * @building: the demolished building
 *
 * Emitted when a building is demolished.
 *
 * Since: 1.0
 */

/**
 * LrgPlacementSystem::validity-changed:
 * @system: the #LrgPlacementSystem
 * @is_valid: whether current placement is valid
 *
 * Emitted when placement validity changes.
 *
 * Since: 1.0
 */

/* Construction */

/**
 * lrg_placement_system_new:
 * @grid: the build grid to place on
 *
 * Creates a new placement system.
 *
 * Returns: (transfer full): A new #LrgPlacementSystem
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlacementSystem *
lrg_placement_system_new (LrgBuildGrid *grid);

/* Grid */

/**
 * lrg_placement_system_get_grid:
 * @self: an #LrgPlacementSystem
 *
 * Gets the build grid.
 *
 * Returns: (transfer none): The grid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildGrid *
lrg_placement_system_get_grid (LrgPlacementSystem *self);

/**
 * lrg_placement_system_set_grid:
 * @self: an #LrgPlacementSystem
 * @grid: the new grid
 *
 * Sets the build grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_set_grid (LrgPlacementSystem *self,
                               LrgBuildGrid       *grid);

/* State */

/**
 * lrg_placement_system_get_state:
 * @self: an #LrgPlacementSystem
 *
 * Gets the current placement state.
 *
 * Returns: Current state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlacementState
lrg_placement_system_get_state (LrgPlacementSystem *self);

/**
 * lrg_placement_system_is_placing:
 * @self: an #LrgPlacementSystem
 *
 * Checks if currently in placement mode.
 *
 * Returns: %TRUE if placing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_system_is_placing (LrgPlacementSystem *self);

/**
 * lrg_placement_system_is_demolishing:
 * @self: an #LrgPlacementSystem
 *
 * Checks if currently in demolition mode.
 *
 * Returns: %TRUE if demolishing
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_system_is_demolishing (LrgPlacementSystem *self);

/* Placement workflow */

/**
 * lrg_placement_system_start_placement:
 * @self: an #LrgPlacementSystem
 * @definition: building definition to place
 *
 * Starts placement mode with the given building.
 *
 * Returns: %TRUE if placement started
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_system_start_placement (LrgPlacementSystem *self,
                                      LrgBuildingDef     *definition);

/**
 * lrg_placement_system_cancel:
 * @self: an #LrgPlacementSystem
 *
 * Cancels current placement or demolition mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_cancel (LrgPlacementSystem *self);

/**
 * lrg_placement_system_update_position:
 * @self: an #LrgPlacementSystem
 * @world_x: World X coordinate
 * @world_y: World Y coordinate
 *
 * Updates the ghost position based on world coordinates.
 * The position will be snapped to the grid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_update_position (LrgPlacementSystem *self,
                                      gdouble             world_x,
                                      gdouble             world_y);

/**
 * lrg_placement_system_set_grid_position:
 * @self: an #LrgPlacementSystem
 * @grid_x: Grid X coordinate
 * @grid_y: Grid Y coordinate
 *
 * Sets the ghost position directly in grid coordinates.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_set_grid_position (LrgPlacementSystem *self,
                                        gint                grid_x,
                                        gint                grid_y);

/**
 * lrg_placement_system_get_grid_position:
 * @self: an #LrgPlacementSystem
 * @grid_x: (out): Grid X coordinate
 * @grid_y: (out): Grid Y coordinate
 *
 * Gets the current ghost grid position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_get_grid_position (LrgPlacementSystem *self,
                                        gint               *grid_x,
                                        gint               *grid_y);

/* Rotation */

/**
 * lrg_placement_system_rotate_cw:
 * @self: an #LrgPlacementSystem
 *
 * Rotates the building 90 degrees clockwise.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_rotate_cw (LrgPlacementSystem *self);

/**
 * lrg_placement_system_rotate_ccw:
 * @self: an #LrgPlacementSystem
 *
 * Rotates the building 90 degrees counter-clockwise.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_rotate_ccw (LrgPlacementSystem *self);

/**
 * lrg_placement_system_get_rotation:
 * @self: an #LrgPlacementSystem
 *
 * Gets the current rotation.
 *
 * Returns: Current rotation
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRotation
lrg_placement_system_get_rotation (LrgPlacementSystem *self);

/**
 * lrg_placement_system_set_rotation:
 * @self: an #LrgPlacementSystem
 * @rotation: New rotation
 *
 * Sets the rotation.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_set_rotation (LrgPlacementSystem *self,
                                   LrgRotation         rotation);

/* Validation */

/**
 * lrg_placement_system_is_valid:
 * @self: an #LrgPlacementSystem
 *
 * Checks if current placement is valid.
 *
 * Returns: %TRUE if placement is valid
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_system_is_valid (LrgPlacementSystem *self);

/**
 * lrg_placement_system_get_current_definition:
 * @self: an #LrgPlacementSystem
 *
 * Gets the building definition being placed.
 *
 * Returns: (transfer none) (nullable): The definition, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingDef *
lrg_placement_system_get_current_definition (LrgPlacementSystem *self);

/* Confirmation */

/**
 * lrg_placement_system_confirm:
 * @self: an #LrgPlacementSystem
 *
 * Confirms placement at the current position.
 * Does NOT deduct resources - that should be handled by the caller
 * after receiving the placement-confirmed signal.
 *
 * Returns: (transfer none) (nullable): The placed building, or %NULL on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_placement_system_confirm (LrgPlacementSystem *self);

/**
 * lrg_placement_system_confirm_and_continue:
 * @self: an #LrgPlacementSystem
 *
 * Confirms placement and stays in placement mode for the same building.
 * Useful for placing multiple of the same building type.
 *
 * Returns: (transfer none) (nullable): The placed building, or %NULL on failure
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_placement_system_confirm_and_continue (LrgPlacementSystem *self);

/* Demolition */

/**
 * lrg_placement_system_start_demolition:
 * @self: an #LrgPlacementSystem
 *
 * Enters demolition mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_start_demolition (LrgPlacementSystem *self);

/**
 * lrg_placement_system_demolish_at:
 * @self: an #LrgPlacementSystem
 * @grid_x: Grid X coordinate
 * @grid_y: Grid Y coordinate
 *
 * Demolishes the building at the given grid position.
 *
 * Returns: (transfer none) (nullable): The demolished building, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_placement_system_demolish_at (LrgPlacementSystem *self,
                                  gint                grid_x,
                                  gint                grid_y);

/**
 * lrg_placement_system_get_building_at_cursor:
 * @self: an #LrgPlacementSystem
 *
 * Gets the building under the current cursor position.
 *
 * Returns: (transfer none) (nullable): The building, or %NULL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgBuildingInstance *
lrg_placement_system_get_building_at_cursor (LrgPlacementSystem *self);

/* Resource validation callback */

/**
 * LrgPlacementResourceCheck:
 * @definition: Building definition to check
 * @level: Building level (1 for new building)
 * @user_data: User data
 *
 * Callback to check if resources are available.
 *
 * Returns: %TRUE if resources are available
 *
 * Since: 1.0
 */
typedef gboolean (*LrgPlacementResourceCheck) (LrgBuildingDef *definition,
                                               gint            level,
                                               gpointer        user_data);

/**
 * lrg_placement_system_set_resource_check:
 * @self: an #LrgPlacementSystem
 * @check: (nullable): Resource check callback
 * @user_data: (nullable): User data for callback
 * @destroy: (nullable): Destroy function for user_data
 *
 * Sets a callback to validate resource availability.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_system_set_resource_check (LrgPlacementSystem        *self,
                                         LrgPlacementResourceCheck  check,
                                         gpointer                   user_data,
                                         GDestroyNotify             destroy);

G_END_DECLS
