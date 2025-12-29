/* lrg-weather-effect.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Base class for weather effects.
 *
 * Weather effects represent visual and audio phenomena like rain,
 * snow, fog, and lightning. This is the abstract base class that
 * provides common functionality for all weather effects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

#define LRG_TYPE_WEATHER_EFFECT (lrg_weather_effect_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWeatherEffect, lrg_weather_effect, LRG, WEATHER_EFFECT, GObject)

/**
 * LrgWeatherEffectClass:
 * @parent_class: Parent class
 * @activate: Activate the effect
 * @deactivate: Deactivate the effect
 * @update: Update the effect state
 * @render: Render the effect
 * @set_intensity: Set effect intensity
 * @set_wind: Apply wind to the effect
 *
 * Virtual table for #LrgWeatherEffect.
 *
 * Since: 1.0
 */
struct _LrgWeatherEffectClass
{
    GObjectClass parent_class;

    /*< public >*/

    void     (*activate)      (LrgWeatherEffect *self);
    void     (*deactivate)    (LrgWeatherEffect *self);
    void     (*update)        (LrgWeatherEffect *self,
                               gfloat            delta_time);
    void     (*render)        (LrgWeatherEffect *self);
    void     (*set_intensity) (LrgWeatherEffect *self,
                               gfloat            intensity);
    void     (*set_wind)      (LrgWeatherEffect *self,
                               gfloat            wind_x,
                               gfloat            wind_y);

    /*< private >*/
    gpointer _reserved[8];
};

/* Properties */

/**
 * lrg_weather_effect_get_id:
 * @self: A #LrgWeatherEffect
 *
 * Gets the effect ID.
 *
 * Returns: (transfer none) (nullable): The effect ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_weather_effect_get_id               (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_id:
 * @self: A #LrgWeatherEffect
 * @id: The effect ID
 *
 * Sets the effect ID.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_id               (LrgWeatherEffect *self,
                                                             const gchar      *id);

/**
 * lrg_weather_effect_is_active:
 * @self: A #LrgWeatherEffect
 *
 * Gets whether the effect is active.
 *
 * Returns: %TRUE if active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_weather_effect_is_active            (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_get_intensity:
 * @self: A #LrgWeatherEffect
 *
 * Gets the current intensity.
 *
 * Returns: The intensity from 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_effect_get_intensity        (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_intensity:
 * @self: A #LrgWeatherEffect
 * @intensity: Intensity from 0.0 to 1.0
 *
 * Sets the effect intensity.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_intensity        (LrgWeatherEffect *self,
                                                             gfloat            intensity);

/**
 * lrg_weather_effect_get_target_intensity:
 * @self: A #LrgWeatherEffect
 *
 * Gets the target intensity for transitions.
 *
 * Returns: The target intensity from 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_effect_get_target_intensity (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_target_intensity:
 * @self: A #LrgWeatherEffect
 * @intensity: Target intensity from 0.0 to 1.0
 *
 * Sets the target intensity. The effect will transition to this
 * intensity over time.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_target_intensity (LrgWeatherEffect *self,
                                                             gfloat            intensity);

/**
 * lrg_weather_effect_get_transition_speed:
 * @self: A #LrgWeatherEffect
 *
 * Gets the intensity transition speed.
 *
 * Returns: Speed in units per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_effect_get_transition_speed (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_transition_speed:
 * @self: A #LrgWeatherEffect
 * @speed: Speed in units per second
 *
 * Sets the intensity transition speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_transition_speed (LrgWeatherEffect *self,
                                                             gfloat            speed);

/* Wind */

/**
 * lrg_weather_effect_get_wind:
 * @self: A #LrgWeatherEffect
 * @wind_x: (out) (nullable): Return location for X wind
 * @wind_y: (out) (nullable): Return location for Y wind
 *
 * Gets the current wind vector.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_get_wind             (LrgWeatherEffect *self,
                                                             gfloat           *wind_x,
                                                             gfloat           *wind_y);

/**
 * lrg_weather_effect_set_wind:
 * @self: A #LrgWeatherEffect
 * @wind_x: X wind component
 * @wind_y: Y wind component
 *
 * Sets the wind vector affecting this effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_wind             (LrgWeatherEffect *self,
                                                             gfloat            wind_x,
                                                             gfloat            wind_y);

/* Control */

/**
 * lrg_weather_effect_activate:
 * @self: A #LrgWeatherEffect
 *
 * Activates the weather effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_activate             (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_deactivate:
 * @self: A #LrgWeatherEffect
 *
 * Deactivates the weather effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_deactivate           (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_update:
 * @self: A #LrgWeatherEffect
 * @delta_time: Time since last update in seconds
 *
 * Updates the effect state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_update               (LrgWeatherEffect *self,
                                                             gfloat            delta_time);

/**
 * lrg_weather_effect_render:
 * @self: A #LrgWeatherEffect
 *
 * Renders the effect.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_render               (LrgWeatherEffect *self);

/* Layer and priority */

/**
 * lrg_weather_effect_get_render_layer:
 * @self: A #LrgWeatherEffect
 *
 * Gets the render layer (for ordering effects).
 *
 * Returns: The render layer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gint                lrg_weather_effect_get_render_layer     (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_render_layer:
 * @self: A #LrgWeatherEffect
 * @layer: The render layer
 *
 * Sets the render layer.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_render_layer     (LrgWeatherEffect *self,
                                                             gint              layer);

G_END_DECLS
