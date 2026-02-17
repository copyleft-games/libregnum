/* lrg-tycoon-template.h - Tycoon/management game template
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base template for tycoon, management, and city-builder games.
 *
 * This template extends LrgGame2DTemplate with management-specific features:
 * - Time control system (pause, speeds 1x-4x)
 * - Resource tracking and display
 * - Grid-based camera panning and zooming
 * - Building placement mode integration
 * - Economy tick updates
 * - Overlay system for different data views
 *
 * Subclass this template for business sims, city builders, factory games,
 * hospital management, theme park tycoons, etc.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-game-2d-template.h"

G_BEGIN_DECLS

#define LRG_TYPE_TYCOON_TEMPLATE (lrg_tycoon_template_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTycoonTemplate, lrg_tycoon_template,
                          LRG, TYCOON_TEMPLATE, LrgGame2DTemplate)

/**
 * LrgTimeSpeed:
 * @LRG_TIME_PAUSED: Game is paused (0x speed)
 * @LRG_TIME_NORMAL: Normal speed (1x)
 * @LRG_TIME_FAST: Fast speed (2x)
 * @LRG_TIME_FASTER: Faster speed (3x)
 * @LRG_TIME_FASTEST: Fastest speed (4x)
 *
 * Time control speeds for management games.
 */
typedef enum
{
    LRG_TIME_PAUSED = 0,
    LRG_TIME_NORMAL,
    LRG_TIME_FAST,
    LRG_TIME_FASTER,
    LRG_TIME_FASTEST
} LrgTimeSpeed;

/**
 * LrgTycoonOverlay:
 * @LRG_TYCOON_OVERLAY_NONE: No overlay (normal view)
 * @LRG_TYCOON_OVERLAY_ZONE: Zone/land use overlay
 * @LRG_TYCOON_OVERLAY_VALUE: Property/resource value overlay
 * @LRG_TYCOON_OVERLAY_TRAFFIC: Traffic/flow overlay
 * @LRG_TYCOON_OVERLAY_POWER: Power/utility overlay
 * @LRG_TYCOON_OVERLAY_WATER: Water/plumbing overlay
 * @LRG_TYCOON_OVERLAY_POLLUTION: Pollution/environment overlay
 * @LRG_TYCOON_OVERLAY_HAPPINESS: Happiness/satisfaction overlay
 * @LRG_TYCOON_OVERLAY_CUSTOM: Custom game-specific overlay
 *
 * Data visualization overlays common in management games.
 */
typedef enum
{
    LRG_TYCOON_OVERLAY_NONE = 0,
    LRG_TYCOON_OVERLAY_ZONE,
    LRG_TYCOON_OVERLAY_VALUE,
    LRG_TYCOON_OVERLAY_TRAFFIC,
    LRG_TYCOON_OVERLAY_POWER,
    LRG_TYCOON_OVERLAY_WATER,
    LRG_TYCOON_OVERLAY_POLLUTION,
    LRG_TYCOON_OVERLAY_HAPPINESS,
    LRG_TYCOON_OVERLAY_CUSTOM
} LrgTycoonOverlay;

/**
 * LrgTycoonTemplateClass:
 * @parent_class: parent class
 * @on_time_speed_changed: called when time speed changes
 * @on_overlay_changed: called when data overlay changes
 * @on_economy_tick: called each economy simulation tick
 * @on_day_changed: called when in-game day advances
 * @on_build_mode_enter: called when entering build/placement mode
 * @on_build_mode_exit: called when exiting build/placement mode
 * @update_economy: updates economy simulation
 * @draw_overlay: renders the current data overlay
 * @draw_grid: renders the placement grid
 * @draw_resources_hud: draws resource counters
 *
 * Class structure for #LrgTycoonTemplate.
 *
 * Subclasses should override these methods to implement
 * game-specific economy simulation and visualization.
 */
struct _LrgTycoonTemplateClass
{
    LrgGame2DTemplateClass parent_class;

    /*< public >*/

    /**
     * LrgTycoonTemplateClass::on_time_speed_changed:
     * @self: a #LrgTycoonTemplate
     * @old_speed: previous time speed
     * @new_speed: new time speed
     *
     * Called when the game time speed changes.
     *
     * Override to update audio, animations, or UI indicators.
     */
    void (*on_time_speed_changed) (LrgTycoonTemplate *self,
                                   LrgTimeSpeed       old_speed,
                                   LrgTimeSpeed       new_speed);

    /**
     * LrgTycoonTemplateClass::on_overlay_changed:
     * @self: a #LrgTycoonTemplate
     * @old_overlay: previous overlay
     * @new_overlay: new overlay
     *
     * Called when the data overlay changes.
     *
     * Override to prepare overlay-specific data or visuals.
     */
    void (*on_overlay_changed) (LrgTycoonTemplate *self,
                                LrgTycoonOverlay   old_overlay,
                                LrgTycoonOverlay   new_overlay);

    /**
     * LrgTycoonTemplateClass::on_economy_tick:
     * @self: a #LrgTycoonTemplate
     *
     * Called on each economy simulation tick.
     *
     * Override to implement resource production, consumption,
     * income/expense calculations, etc.
     */
    void (*on_economy_tick) (LrgTycoonTemplate *self);

    /**
     * LrgTycoonTemplateClass::on_day_changed:
     * @self: a #LrgTycoonTemplate
     * @day: the new day number
     *
     * Called when the in-game day advances.
     *
     * Override for daily events, reports, or state changes.
     */
    void (*on_day_changed) (LrgTycoonTemplate *self,
                            guint              day);

    /**
     * LrgTycoonTemplateClass::on_build_mode_enter:
     * @self: a #LrgTycoonTemplate
     *
     * Called when entering building placement mode.
     */
    void (*on_build_mode_enter) (LrgTycoonTemplate *self);

    /**
     * LrgTycoonTemplateClass::on_build_mode_exit:
     * @self: a #LrgTycoonTemplate
     *
     * Called when exiting building placement mode.
     */
    void (*on_build_mode_exit) (LrgTycoonTemplate *self);

    /**
     * LrgTycoonTemplateClass::update_economy:
     * @self: a #LrgTycoonTemplate
     * @delta: scaled time since last frame
     *
     * Updates economy simulation.
     *
     * The delta is already scaled by time speed.
     */
    void (*update_economy) (LrgTycoonTemplate *self,
                            gdouble            delta);

    /**
     * LrgTycoonTemplateClass::draw_overlay:
     * @self: a #LrgTycoonTemplate
     *
     * Renders the current data overlay.
     *
     * The default draws nothing. Override to visualize
     * zone colors, heat maps, flow arrows, etc.
     */
    void (*draw_overlay) (LrgTycoonTemplate *self);

    /**
     * LrgTycoonTemplateClass::draw_grid:
     * @self: a #LrgTycoonTemplate
     *
     * Renders the placement grid.
     *
     * Called when in build mode or when grid is enabled.
     */
    void (*draw_grid) (LrgTycoonTemplate *self);

    /**
     * LrgTycoonTemplateClass::draw_resources_hud:
     * @self: a #LrgTycoonTemplate
     *
     * Draws resource counters and status indicators.
     *
     * Override to display money, resources, ratings, etc.
     */
    void (*draw_resources_hud) (LrgTycoonTemplate *self);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_tycoon_template_new:
 *
 * Creates a new tycoon game template with default settings.
 *
 * Returns: (transfer full): a new #LrgTycoonTemplate
 */
LRG_AVAILABLE_IN_ALL
LrgTycoonTemplate * lrg_tycoon_template_new (void);

/* ==========================================================================
 * Time Control
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_time_speed:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the current time speed.
 *
 * Returns: the #LrgTimeSpeed
 */
LRG_AVAILABLE_IN_ALL
LrgTimeSpeed lrg_tycoon_template_get_time_speed (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_time_speed:
 * @self: a #LrgTycoonTemplate
 * @speed: the time speed to set
 *
 * Sets the game time speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_time_speed (LrgTycoonTemplate *self,
                                         LrgTimeSpeed       speed);

/**
 * lrg_tycoon_template_toggle_pause:
 * @self: a #LrgTycoonTemplate
 *
 * Toggles between paused and previous speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_toggle_pause (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_is_paused:
 * @self: a #LrgTycoonTemplate
 *
 * Checks if the game is paused.
 *
 * Returns: %TRUE if paused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tycoon_template_is_paused (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_get_time_multiplier:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the current time multiplier as a float.
 *
 * Returns: 0.0 for paused, 1.0 for normal, 2.0 for fast, etc.
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_time_multiplier (LrgTycoonTemplate *self);

/* ==========================================================================
 * In-Game Time
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_day:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the current in-game day number.
 *
 * Returns: the day number (starting from 1)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_tycoon_template_get_day (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_day:
 * @self: a #LrgTycoonTemplate
 * @day: the day number
 *
 * Sets the current in-game day.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_day (LrgTycoonTemplate *self,
                                  guint              day);

/**
 * lrg_tycoon_template_get_day_progress:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the progress through the current day (0.0-1.0).
 *
 * Returns: progress from 0.0 (midnight) to 1.0 (end of day)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_day_progress (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_get_day_length:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the length of an in-game day in real seconds (at 1x speed).
 *
 * Returns: day length in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_day_length (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_day_length:
 * @self: a #LrgTycoonTemplate
 * @seconds: day length in real seconds
 *
 * Sets the length of an in-game day.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_day_length (LrgTycoonTemplate *self,
                                         gfloat             seconds);

/* ==========================================================================
 * Economy Tick
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_tick_interval:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the economy tick interval in real seconds (at 1x speed).
 *
 * Returns: tick interval in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_tick_interval (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_tick_interval:
 * @self: a #LrgTycoonTemplate
 * @seconds: tick interval in real seconds
 *
 * Sets the economy tick interval.
 *
 * Economy updates happen at this interval, scaled by time speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_tick_interval (LrgTycoonTemplate *self,
                                            gfloat             seconds);

/* ==========================================================================
 * Data Overlay
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_overlay:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the current data overlay.
 *
 * Returns: the #LrgTycoonOverlay
 */
LRG_AVAILABLE_IN_ALL
LrgTycoonOverlay lrg_tycoon_template_get_overlay (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_overlay:
 * @self: a #LrgTycoonTemplate
 * @overlay: the overlay to display
 *
 * Sets the data overlay to display.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_overlay (LrgTycoonTemplate *self,
                                      LrgTycoonOverlay   overlay);

/* ==========================================================================
 * Build Mode
 * ========================================================================== */

/**
 * lrg_tycoon_template_is_build_mode:
 * @self: a #LrgTycoonTemplate
 *
 * Checks if currently in building placement mode.
 *
 * Returns: %TRUE if in build mode
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tycoon_template_is_build_mode (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_enter_build_mode:
 * @self: a #LrgTycoonTemplate
 *
 * Enters building placement mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_enter_build_mode (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_exit_build_mode:
 * @self: a #LrgTycoonTemplate
 *
 * Exits building placement mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_exit_build_mode (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_get_show_grid:
 * @self: a #LrgTycoonTemplate
 *
 * Gets whether the placement grid is visible.
 *
 * Returns: %TRUE if grid is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tycoon_template_get_show_grid (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_show_grid:
 * @self: a #LrgTycoonTemplate
 * @show: whether to show the grid
 *
 * Sets grid visibility.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_show_grid (LrgTycoonTemplate *self,
                                        gboolean           show);

/* ==========================================================================
 * Grid Settings
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_grid_size:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the grid cell size in world units.
 *
 * Returns: the grid cell size
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_grid_size (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_grid_size:
 * @self: a #LrgTycoonTemplate
 * @size: the grid cell size in world units
 *
 * Sets the grid cell size.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_grid_size (LrgTycoonTemplate *self,
                                        gfloat             size);

/**
 * lrg_tycoon_template_snap_to_grid:
 * @self: a #LrgTycoonTemplate
 * @x: input X coordinate
 * @y: input Y coordinate
 * @grid_x: (out): snapped X coordinate
 * @grid_y: (out): snapped Y coordinate
 *
 * Snaps world coordinates to the nearest grid cell.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_snap_to_grid (LrgTycoonTemplate *self,
                                       gfloat             x,
                                       gfloat             y,
                                       gfloat            *grid_x,
                                       gfloat            *grid_y);

/**
 * lrg_tycoon_template_world_to_grid:
 * @self: a #LrgTycoonTemplate
 * @world_x: world X coordinate
 * @world_y: world Y coordinate
 * @grid_x: (out): grid column
 * @grid_y: (out): grid row
 *
 * Converts world coordinates to grid cell indices.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_world_to_grid (LrgTycoonTemplate *self,
                                        gfloat             world_x,
                                        gfloat             world_y,
                                        gint              *grid_x,
                                        gint              *grid_y);

/**
 * lrg_tycoon_template_grid_to_world:
 * @self: a #LrgTycoonTemplate
 * @grid_x: grid column
 * @grid_y: grid row
 * @world_x: (out): world X coordinate (cell center)
 * @world_y: (out): world Y coordinate (cell center)
 *
 * Converts grid cell indices to world coordinates (cell center).
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_grid_to_world (LrgTycoonTemplate *self,
                                        gint               grid_x,
                                        gint               grid_y,
                                        gfloat            *world_x,
                                        gfloat            *world_y);

/* ==========================================================================
 * Camera Controls
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_pan_speed:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the camera pan speed.
 *
 * Returns: pan speed in world units per second
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_pan_speed (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_pan_speed:
 * @self: a #LrgTycoonTemplate
 * @speed: pan speed in world units per second
 *
 * Sets the camera pan speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_pan_speed (LrgTycoonTemplate *self,
                                        gfloat             speed);

/**
 * lrg_tycoon_template_get_zoom_speed:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the camera zoom speed.
 *
 * Returns: zoom speed (units per scroll tick)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_zoom_speed (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_zoom_speed:
 * @self: a #LrgTycoonTemplate
 * @speed: zoom speed
 *
 * Sets the camera zoom speed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_zoom_speed (LrgTycoonTemplate *self,
                                         gfloat             speed);

/**
 * lrg_tycoon_template_get_min_zoom:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the minimum zoom level.
 *
 * Returns: minimum zoom (smaller = more zoomed out)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_min_zoom (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_get_max_zoom:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the maximum zoom level.
 *
 * Returns: maximum zoom (larger = more zoomed in)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_tycoon_template_get_max_zoom (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_zoom_limits:
 * @self: a #LrgTycoonTemplate
 * @min_zoom: minimum zoom level
 * @max_zoom: maximum zoom level
 *
 * Sets the zoom limits.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_zoom_limits (LrgTycoonTemplate *self,
                                          gfloat             min_zoom,
                                          gfloat             max_zoom);

/**
 * lrg_tycoon_template_get_edge_pan_margin:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the edge pan margin in pixels.
 *
 * Returns: edge pan margin, or 0 if disabled
 */
LRG_AVAILABLE_IN_ALL
gint lrg_tycoon_template_get_edge_pan_margin (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_edge_pan_margin:
 * @self: a #LrgTycoonTemplate
 * @margin: margin in pixels (0 to disable)
 *
 * Sets the edge pan margin.
 *
 * When the mouse is within this margin of the screen edge,
 * the camera pans in that direction.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_edge_pan_margin (LrgTycoonTemplate *self,
                                              gint               margin);

/**
 * lrg_tycoon_template_set_camera_position:
 * @self: a #LrgTycoonTemplate
 * @x: world x coordinate
 * @y: world y coordinate
 *
 * Sets the camera position directly. Useful for centering
 * the camera on a specific world location at game start.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_camera_position (LrgTycoonTemplate *self,
                                               gfloat             x,
                                               gfloat             y);

/**
 * lrg_tycoon_template_set_camera_bounds:
 * @self: a #LrgTycoonTemplate
 * @min_x: minimum camera x position (world coordinates)
 * @min_y: minimum camera y position (world coordinates)
 * @max_x: maximum camera x position (world coordinates)
 * @max_y: maximum camera y position (world coordinates)
 *
 * Sets camera panning bounds. Once set, the camera position is
 * clamped to the given rectangle after every pan operation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_camera_bounds (LrgTycoonTemplate *self,
                                             gfloat             min_x,
                                             gfloat             min_y,
                                             gfloat             max_x,
                                             gfloat             max_y);

/* ==========================================================================
 * Resources (Basic Tracking)
 * ========================================================================== */

/**
 * lrg_tycoon_template_get_money:
 * @self: a #LrgTycoonTemplate
 *
 * Gets the current money balance.
 *
 * Returns: the money amount
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_tycoon_template_get_money (LrgTycoonTemplate *self);

/**
 * lrg_tycoon_template_set_money:
 * @self: a #LrgTycoonTemplate
 * @money: the money amount
 *
 * Sets the money balance.
 */
LRG_AVAILABLE_IN_ALL
void lrg_tycoon_template_set_money (LrgTycoonTemplate *self,
                                    gint64             money);

/**
 * lrg_tycoon_template_add_money:
 * @self: a #LrgTycoonTemplate
 * @amount: amount to add (negative to subtract)
 *
 * Adds to (or subtracts from) the money balance.
 *
 * Returns: the new balance
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_tycoon_template_add_money (LrgTycoonTemplate *self,
                                      gint64             amount);

/**
 * lrg_tycoon_template_can_afford:
 * @self: a #LrgTycoonTemplate
 * @cost: the cost to check
 *
 * Checks if the player can afford a cost.
 *
 * Returns: %TRUE if money >= cost
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_tycoon_template_can_afford (LrgTycoonTemplate *self,
                                         gint64             cost);

G_END_DECLS
