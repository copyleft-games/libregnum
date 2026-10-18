/* lrg-lightning.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Lightning weather effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_LIGHTNING (lrg_lightning_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgLightning, lrg_lightning, LRG, LIGHTNING, LrgWeatherEffect)

/**
 * lrg_lightning_new:
 *
 * Creates a new lightning effect.
 *
 * Returns: (transfer full): A new #LrgLightning
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgLightning *      lrg_lightning_new                   (void);

/* Timing */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_min_interval      (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_min_interval      (LrgLightning *self,
                                                         gfloat        seconds);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_max_interval      (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_max_interval      (LrgLightning *self,
                                                         gfloat        seconds);

/* Flash properties */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_flash_duration    (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_duration    (LrgLightning *self,
                                                         gfloat        duration);

LRG_AVAILABLE_IN_ALL
guint               lrg_lightning_get_flash_count       (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_count       (LrgLightning *self,
                                                         guint         count);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_flash_intensity   (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_flash_intensity   (LrgLightning *self,
                                                         gfloat        intensity);

/* Thunder */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_thunder_enabled   (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_thunder_enabled   (LrgLightning *self,
                                                         gboolean      enabled);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_thunder_delay     (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_thunder_delay     (LrgLightning *self,
                                                         gfloat        delay);

/* Bolt rendering */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_bolts_enabled     (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_bolts_enabled     (LrgLightning *self,
                                                         gboolean      enabled);

/* Manual trigger */

/**
 * lrg_lightning_trigger_flash:
 * @self: A #LrgLightning
 *
 * Starts a strike of flash-count sub-flashes now (a new bolt, the
 * flash-started signal, the thunder countdown), unless one is running.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_lightning_trigger_flash         (LrgLightning *self);

LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_is_flashing           (LrgLightning *self);

/* Color */

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_get_color             (LrgLightning *self,
                                                         guint8       *r,
                                                         guint8       *g,
                                                         guint8       *b);

LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_color             (LrgLightning *self,
                                                         guint8        r,
                                                         guint8        g,
                                                         guint8        b);

/* Distance-delayed thunder */

/**
 * lrg_lightning_set_thunder_from_distance:
 * @self: A #LrgLightning
 * @enabled: %TRUE to delay every thunder by its strike's distance over
 *   %LRG_SPEED_OF_SOUND (lrg_celestial_thunder_delay()) instead of the
 *   thunder delay; random strikes then land between half and one and a
 *   half times the bolt distance away, so near strikes rumble at once
 *   and far ones seconds later
 *
 * Thunders travel independently: a new strike does not cancel the
 * thunder of the previous one (up to 8 are in the air).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_thunder_from_distance (LrgLightning *self,
                                                             gboolean      enabled);

/**
 * lrg_lightning_get_thunder_from_distance:
 * @self: A #LrgLightning
 *
 * Returns: whether thunder is delayed by distance
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_thunder_from_distance (LrgLightning *self);

/**
 * lrg_lightning_strike_at:
 * @self: A #LrgLightning
 * @distance: horizontal distance of the bolt from the camera, world
 *   units (metres)
 * @bearing: radians around the camera: 0 along +X, pi/2 along +Z
 *
 * A strike now at a chosen place (a distant storm on the horizon),
 * even while flashing.  Its thunder follows by distance when thunder
 * from distance is on, else after the thunder delay.  Invalid values
 * are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_lightning_strike_at             (LrgLightning *self,
                                                         gfloat        distance,
                                                         gfloat        bearing);

/**
 * lrg_lightning_get_strike_distance:
 * @self: A #LrgLightning
 *
 * Returns: the horizontal distance of the latest strike
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_strike_distance   (LrgLightning *self);

/**
 * lrg_lightning_get_thunder_distance:
 * @self: A #LrgLightning
 *
 * Read in a #LrgLightning::thunder handler to scale the sound.
 *
 * Returns: the distance of the strike whose thunder was heard last
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_thunder_distance  (LrgLightning *self);

/**
 * lrg_lightning_get_pending_thunder_count:
 * @self: A #LrgLightning
 *
 * Returns: distance-delayed thunders not heard yet
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_lightning_get_pending_thunder_count (LrgLightning *self);

/* Flash level and bolt */

/**
 * lrg_lightning_get_flash_level:
 * @self: A #LrgLightning
 *
 * The brightness of the flash right now: each sub-flash rises within
 * its first 10% (from 60%) and decays quadratically to dark; flicker
 * sub-flashes after the first are at 60%. Scaled by the flash
 * intensity and the effect's intensity. The overlay pass draws the
 * flash colour with this as alpha; a scene can also add it to its
 * ambient light. 0 when not flashing or inactive.
 *
 * Returns: 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_lightning_get_flash_level       (LrgLightning *self);

/**
 * lrg_lightning_get_bolt_shape:
 * @self: A #LrgLightning
 * @distance: (out) (optional): horizontal distance from the camera
 * @height: (out) (optional): height of the bolt's top above the camera
 * @width: (out) (optional): ribbon width
 *
 * Gets the world-space bolt shape (world units).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_lightning_get_bolt_shape        (LrgLightning *self,
                                                         gfloat       *distance,
                                                         gfloat       *height,
                                                         gfloat       *width);

/**
 * lrg_lightning_set_bolt_shape:
 * @self: A #LrgLightning
 * @distance: horizontal distance of the bolt from the camera (default
 *   120)
 * @height: height of its top above the camera (default 80); the bolt
 *   spans 1.25 x this, down to a quarter below the camera, so terrain
 *   hides its foot
 * @width: ribbon width (default 0.8)
 *
 * Shapes the bolts drawn by lrg_weather_effect_render_world() when
 * bolts are enabled; each strike picks a random bearing around the
 * camera of the moment. Invalid values are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_lightning_set_bolt_shape        (LrgLightning *self,
                                                         gfloat        distance,
                                                         gfloat        height,
                                                         gfloat        width);

/**
 * lrg_lightning_get_bolt_point_count:
 * @self: A #LrgLightning
 *
 * Returns: the points of the current strike's bolt polyline, 0 when
 *   not flashing
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_lightning_get_bolt_point_count  (LrgLightning *self);

/**
 * lrg_lightning_get_bolt_point:
 * @self: A #LrgLightning
 * @index: point index, top (0) to bottom
 * @out_position: (out caller-allocates): the point in world space
 *
 * Reads the current bolt. Headless.
 *
 * Returns: %TRUE when @index is a point of a live bolt
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_lightning_get_bolt_point        (LrgLightning *self,
                                                         guint         index,
                                                         GrlVector3   *out_position);

G_END_DECLS
