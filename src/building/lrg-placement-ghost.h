/* lrg-placement-ghost.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgPlacementGhost - Visual preview for building placement.
 *
 * Renders a semi-transparent preview of the building being placed,
 * with different colors indicating valid/invalid placement.
 * Implements the LrgDrawable interface.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../graphics/lrg-drawable.h"
#include "lrg-placement-system.h"

G_BEGIN_DECLS

#define LRG_TYPE_PLACEMENT_GHOST (lrg_placement_ghost_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPlacementGhost, lrg_placement_ghost, LRG, PLACEMENT_GHOST, GObject)

/* Construction */

/**
 * lrg_placement_ghost_new:
 * @system: the placement system to visualize
 *
 * Creates a new placement ghost.
 *
 * Returns: (transfer full): A new #LrgPlacementGhost
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlacementGhost *
lrg_placement_ghost_new (LrgPlacementSystem *system);

/* Placement system */

/**
 * lrg_placement_ghost_get_system:
 * @self: an #LrgPlacementGhost
 *
 * Gets the placement system.
 *
 * Returns: (transfer none): The placement system
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgPlacementSystem *
lrg_placement_ghost_get_system (LrgPlacementGhost *self);

/**
 * lrg_placement_ghost_set_system:
 * @self: an #LrgPlacementGhost
 * @system: the new placement system
 *
 * Sets the placement system.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_system (LrgPlacementGhost  *self,
                                LrgPlacementSystem *system);

/* Colors */

/**
 * lrg_placement_ghost_set_valid_color:
 * @self: an #LrgPlacementGhost
 * @color: color for valid placement
 *
 * Sets the color used when placement is valid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_valid_color (LrgPlacementGhost *self,
                                     GrlColor          *color);

/**
 * lrg_placement_ghost_get_valid_color:
 * @self: an #LrgPlacementGhost
 * @out_color: (out): location to store the color
 *
 * Gets the color used when placement is valid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_get_valid_color (LrgPlacementGhost *self,
                                     GrlColor          *out_color);

/**
 * lrg_placement_ghost_set_invalid_color:
 * @self: an #LrgPlacementGhost
 * @color: color for invalid placement
 *
 * Sets the color used when placement is invalid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_invalid_color (LrgPlacementGhost *self,
                                       GrlColor          *color);

/**
 * lrg_placement_ghost_get_invalid_color:
 * @self: an #LrgPlacementGhost
 * @out_color: (out): location to store the color
 *
 * Gets the color used when placement is invalid.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_get_invalid_color (LrgPlacementGhost *self,
                                       GrlColor          *out_color);

/**
 * lrg_placement_ghost_set_demolish_color:
 * @self: an #LrgPlacementGhost
 * @color: color for demolition mode
 *
 * Sets the color used in demolition mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_demolish_color (LrgPlacementGhost *self,
                                        GrlColor          *color);

/**
 * lrg_placement_ghost_get_demolish_color:
 * @self: an #LrgPlacementGhost
 * @out_color: (out): location to store the color
 *
 * Gets the color used in demolition mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_get_demolish_color (LrgPlacementGhost *self,
                                        GrlColor          *out_color);

/* Visibility */

/**
 * lrg_placement_ghost_get_visible:
 * @self: an #LrgPlacementGhost
 *
 * Gets whether the ghost is visible.
 *
 * Returns: %TRUE if visible
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_ghost_get_visible (LrgPlacementGhost *self);

/**
 * lrg_placement_ghost_set_visible:
 * @self: an #LrgPlacementGhost
 * @visible: whether to show the ghost
 *
 * Sets whether the ghost is visible.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_visible (LrgPlacementGhost *self,
                                 gboolean           visible);

/* Grid lines */

/**
 * lrg_placement_ghost_get_show_grid:
 * @self: an #LrgPlacementGhost
 *
 * Gets whether grid lines are shown.
 *
 * Returns: %TRUE if grid is shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_placement_ghost_get_show_grid (LrgPlacementGhost *self);

/**
 * lrg_placement_ghost_set_show_grid:
 * @self: an #LrgPlacementGhost
 * @show_grid: whether to show grid lines
 *
 * Sets whether to show grid lines around the ghost.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_show_grid (LrgPlacementGhost *self,
                                   gboolean           show_grid);

/**
 * lrg_placement_ghost_set_grid_color:
 * @self: an #LrgPlacementGhost
 * @color: grid line color
 *
 * Sets the grid line color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_grid_color (LrgPlacementGhost *self,
                                    GrlColor          *color);

/* Custom rendering callback */

/**
 * LrgPlacementGhostDrawFunc:
 * @ghost: the #LrgPlacementGhost
 * @definition: the building definition being placed
 * @world_x: world X coordinate
 * @world_y: world Y coordinate
 * @width: building width in world units
 * @height: building height in world units
 * @rotation: building rotation
 * @is_valid: whether placement is valid
 * @user_data: user data
 *
 * Custom drawing function for the placement ghost.
 * If set, this is called instead of the default drawing.
 *
 * Since: 1.0
 */
typedef void (*LrgPlacementGhostDrawFunc) (LrgPlacementGhost *ghost,
                                           LrgBuildingDef    *definition,
                                           gdouble            world_x,
                                           gdouble            world_y,
                                           gdouble            width,
                                           gdouble            height,
                                           LrgRotation        rotation,
                                           gboolean           is_valid,
                                           gpointer           user_data);

/**
 * lrg_placement_ghost_set_draw_func:
 * @self: an #LrgPlacementGhost
 * @func: (nullable): custom drawing function
 * @user_data: (nullable): user data for callback
 * @destroy: (nullable): destroy function for user_data
 *
 * Sets a custom drawing function.
 * If set, the custom function is called instead of the default drawing.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_placement_ghost_set_draw_func (LrgPlacementGhost         *self,
                                   LrgPlacementGhostDrawFunc  func,
                                   gpointer                   user_data,
                                   GDestroyNotify             destroy);

G_END_DECLS
