/* lrg-weather-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Weather system manager.
 *
 * Manages weather states, transitions between them, and integrates with
 * the day/night cycle.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather.h"
#include "lrg-day-night-cycle.h"

G_BEGIN_DECLS

#define LRG_TYPE_WEATHER_MANAGER (lrg_weather_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWeatherManager, lrg_weather_manager, LRG, WEATHER_MANAGER, GObject)

/**
 * lrg_weather_manager_new:
 *
 * Creates a new weather manager.
 *
 * Returns: (transfer full): A new #LrgWeatherManager
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherManager * lrg_weather_manager_new (void);

/* Weather registration */

/**
 * lrg_weather_manager_register_weather:
 * @self: an #LrgWeatherManager
 * @weather: (transfer none): an #LrgWeather to register
 *
 * Registers a weather state with the manager.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_register_weather (LrgWeatherManager *self,
                                           LrgWeather        *weather);

/**
 * lrg_weather_manager_unregister_weather:
 * @self: an #LrgWeatherManager
 * @weather_id: the weather ID to unregister
 *
 * Unregisters a weather state.
 *
 * Returns: %TRUE if successfully unregistered
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_manager_unregister_weather (LrgWeatherManager *self,
                                                 const gchar       *weather_id);

/**
 * lrg_weather_manager_get_weather:
 * @self: an #LrgWeatherManager
 * @weather_id: the weather ID to look up
 *
 * Gets a registered weather state by ID.
 *
 * Returns: (transfer none) (nullable): The #LrgWeather, or %NULL if not found
 */
LRG_AVAILABLE_IN_ALL
LrgWeather * lrg_weather_manager_get_weather (LrgWeatherManager *self,
                                              const gchar       *weather_id);

/**
 * lrg_weather_manager_get_registered_weather:
 * @self: an #LrgWeatherManager
 *
 * Gets all registered weather states.
 *
 * Returns: (transfer container) (element-type LrgWeather): List of registered weather
 */
LRG_AVAILABLE_IN_ALL
GList * lrg_weather_manager_get_registered_weather (LrgWeatherManager *self);

/* Active weather control */

/**
 * lrg_weather_manager_set_weather:
 * @self: an #LrgWeatherManager
 * @weather_id: (nullable): the weather ID to activate, or %NULL for clear
 * @transition_duration: duration of transition in seconds (0 = instant)
 *
 * Sets the active weather state.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_set_weather (LrgWeatherManager *self,
                                      const gchar       *weather_id,
                                      gfloat             transition_duration);

/**
 * lrg_weather_manager_get_active_weather:
 * @self: an #LrgWeatherManager
 *
 * Gets the currently active weather state.
 *
 * Returns: (transfer none) (nullable): The active #LrgWeather, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWeather * lrg_weather_manager_get_active_weather (LrgWeatherManager *self);

/**
 * lrg_weather_manager_get_active_weather_id:
 * @self: an #LrgWeatherManager
 *
 * Gets the ID of the currently active weather.
 *
 * Returns: (nullable): The active weather ID, or %NULL
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_weather_manager_get_active_weather_id (LrgWeatherManager *self);

/**
 * lrg_weather_manager_is_transitioning:
 * @self: an #LrgWeatherManager
 *
 * Gets whether a weather transition is in progress.
 *
 * Returns: %TRUE if transitioning
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_manager_is_transitioning (LrgWeatherManager *self);

/**
 * lrg_weather_manager_clear_weather:
 * @self: an #LrgWeatherManager
 * @transition_duration: duration of transition in seconds
 *
 * Clears all active weather (transitions to clear).
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_clear_weather (LrgWeatherManager *self,
                                        gfloat             transition_duration);

/* Wind */

/**
 * lrg_weather_manager_get_wind:
 * @self: an #LrgWeatherManager
 * @wind_x: (out) (nullable): X wind component
 * @wind_y: (out) (nullable): Y wind component
 *
 * Gets the current global wind vector.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_get_wind (LrgWeatherManager *self,
                                   gfloat            *wind_x,
                                   gfloat            *wind_y);

/**
 * lrg_weather_manager_set_wind:
 * @self: an #LrgWeatherManager
 * @wind_x: X wind component
 * @wind_y: Y wind component
 *
 * Sets the global wind vector (affects all weather effects).
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_set_wind (LrgWeatherManager *self,
                                   gfloat             wind_x,
                                   gfloat             wind_y);

/* Day/night cycle integration */

/**
 * lrg_weather_manager_get_day_night_cycle:
 * @self: an #LrgWeatherManager
 *
 * Gets the day/night cycle manager.
 *
 * Returns: (transfer none): The #LrgDayNightCycle
 */
LRG_AVAILABLE_IN_ALL
LrgDayNightCycle * lrg_weather_manager_get_day_night_cycle (LrgWeatherManager *self);

/**
 * lrg_weather_manager_get_day_night_enabled:
 * @self: an #LrgWeatherManager
 *
 * Gets whether day/night cycle affects ambient lighting.
 *
 * Returns: %TRUE if day/night cycle is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_weather_manager_get_day_night_enabled (LrgWeatherManager *self);

/**
 * lrg_weather_manager_set_day_night_enabled:
 * @self: an #LrgWeatherManager
 * @enabled: whether to enable day/night cycle
 *
 * Enables or disables day/night cycle integration.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_set_day_night_enabled (LrgWeatherManager *self,
                                                gboolean           enabled);

/* Ambient */

/**
 * lrg_weather_manager_get_combined_ambient:
 * @self: an #LrgWeatherManager
 * @r: (out) (nullable): red component
 * @g: (out) (nullable): green component
 * @b: (out) (nullable): blue component
 * @brightness: (out) (nullable): brightness multiplier
 *
 * Gets the combined ambient color and brightness from weather and day/night.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_get_combined_ambient (LrgWeatherManager *self,
                                               guint8            *r,
                                               guint8            *g,
                                               guint8            *b,
                                               gfloat            *brightness);

/* Update and render */

/**
 * lrg_weather_manager_update:
 * @self: an #LrgWeatherManager
 * @delta_time: time elapsed in seconds
 *
 * Updates the weather system.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_update (LrgWeatherManager *self,
                                 gfloat             delta_time);

/**
 * lrg_weather_manager_render:
 * @self: an #LrgWeatherManager
 *
 * Renders all active weather effects.
 */
LRG_AVAILABLE_IN_ALL
void lrg_weather_manager_render (LrgWeatherManager *self);

G_END_DECLS
