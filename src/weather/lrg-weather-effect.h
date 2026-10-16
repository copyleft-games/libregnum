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
 *
 * Effects draw in up to three passes: render() (the 2D,
 * %LRG_WEATHER_SPACE_SCREEN pass), render_world() (inside 3D mode, for
 * %LRG_WEATHER_SPACE_WORLD particles and lightning bolts) and
 * render_overlay() (screen space after the 3D pass: fog veil, lightning
 * flash).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
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
 * @render_world: Draw the 3D pass around the camera (inside 3D mode)
 * @render_overlay: Draw the screen-space pass after the 3D pass
 * @space_changed: React to lrg_weather_effect_set_space() (the new
 *   space is already stored)
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
    void     (*render_world)  (LrgWeatherEffect *self,
                               const GrlVector3 *camera_position);
    void     (*render_overlay) (LrgWeatherEffect *self,
                                gint              width,
                                gint              height);
    void     (*space_changed) (LrgWeatherEffect *self);

    /*< private >*/
    gpointer _reserved[5];
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
 * Sets the wind vector affecting this effect. In
 * %LRG_WEATHER_SPACE_SCREEN it is in pixels per second (y pointing
 * down the screen); in %LRG_WEATHER_SPACE_WORLD it is the horizontal
 * wind in world units per second along X and Z.
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
 * Renders the effect's 2D pass: %LRG_WEATHER_SPACE_SCREEN rain and
 * snow in their area, and full-screen overlays (fog veil, lightning
 * flash and bolt) for 2D games. 3D scenes use
 * lrg_weather_effect_render_world() and
 * lrg_weather_effect_render_overlay() instead. The built-in effects
 * draw nothing without a window.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_render               (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_render_world:
 * @self: A #LrgWeatherEffect
 * @camera_position: the eye of the active 3D camera, in world units
 *
 * Draws the effect's 3D pass: %LRG_WEATHER_SPACE_WORLD rain streaks
 * and snow flakes in their volume around @camera_position, lightning
 * bolts. Call between BeginMode3D() and EndMode3D(), after opaque
 * geometry (the particles test depth but do not write it). Remembers
 * @camera_position (lrg_weather_effect_get_camera_position()) for the
 * next update. The built-in effects draw nothing without a window.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_render_world         (LrgWeatherEffect *self,
                                                             const GrlVector3 *camera_position);

/**
 * lrg_weather_effect_render_overlay:
 * @self: A #LrgWeatherEffect
 * @width: screen (or render target) width in pixels
 * @height: screen (or render target) height in pixels
 *
 * Draws the effect's screen-space pass for a 3D scene: the #LrgFog
 * veil and the #LrgLightning flash. Call after EndMode3D(), before the
 * UI. A no-op with a non-positive size; the built-in effects draw
 * nothing without a window.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_render_overlay       (LrgWeatherEffect *self,
                                                             gint              width,
                                                             gint              height);

/* Space, volume and camera */

/**
 * lrg_weather_effect_get_space:
 * @self: A #LrgWeatherEffect
 *
 * Returns: where the effect simulates and draws its particles
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherSpace     lrg_weather_effect_get_space            (LrgWeatherEffect *self);

/**
 * lrg_weather_effect_set_space:
 * @self: A #LrgWeatherEffect
 * @space: the new space
 *
 * Switches between 2D (pixels, an area, render()) and 3D (world units,
 * a volume around the camera, render_world()). Particle effects reset
 * their unit-dependent settings (speeds, lengths, sizes, splash height)
 * to the new space's defaults, so set those after the space. The
 * default is %LRG_WEATHER_SPACE_SCREEN.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_space            (LrgWeatherEffect *self,
                                                             LrgWeatherSpace   space);

/**
 * lrg_weather_effect_get_volume:
 * @self: A #LrgWeatherEffect
 * @radius: (out) (optional): half the volume's width along X and Z
 * @height: (out) (optional): the volume's full height
 *
 * Gets the %LRG_WEATHER_SPACE_WORLD particle volume, a box centred on
 * the camera.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_get_volume           (LrgWeatherEffect *self,
                                                             gfloat           *radius,
                                                             gfloat           *height);

/**
 * lrg_weather_effect_set_volume:
 * @self: A #LrgWeatherEffect
 * @radius: half the box's width along X and Z (default 15)
 * @height: the box's full height (default 12)
 *
 * Sets the box around the camera that %LRG_WEATHER_SPACE_WORLD
 * particles fill. Particles stay put in the world and wrap around the
 * box as the camera moves, so walking through rain shows parallax and
 * never empties the volume. Non-positive or non-finite values are
 * ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_volume           (LrgWeatherEffect *self,
                                                             gfloat            radius,
                                                             gfloat            height);

/**
 * lrg_weather_effect_get_camera_position:
 * @self: A #LrgWeatherEffect
 * @out_position: (out caller-allocates): the camera the volume is
 *   centred on
 *
 * Gets the camera set by lrg_weather_effect_set_camera_position() or
 * the last lrg_weather_effect_render_world(); (0, 0, 0) at first.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_get_camera_position  (LrgWeatherEffect *self,
                                                             GrlVector3       *out_position);

/**
 * lrg_weather_effect_set_camera_position:
 * @self: A #LrgWeatherEffect
 * @position: the camera eye in world units
 *
 * Centres the %LRG_WEATHER_SPACE_WORLD volume (and lightning bolts) on
 * @position for the next update without drawing; useful headless or
 * when the update runs before the draw. Non-finite positions are
 * ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_effect_set_camera_position  (LrgWeatherEffect *self,
                                                             const GrlVector3 *position);

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
