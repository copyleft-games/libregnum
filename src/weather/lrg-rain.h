/* lrg-rain.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Rain weather effect.
 *
 * Creates rain particles with configurable density, speed,
 * and wind interaction.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_RAIN (lrg_rain_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgRain, lrg_rain, LRG, RAIN, LrgWeatherEffect)

/**
 * lrg_rain_new:
 *
 * Creates a new rain effect.
 *
 * Returns: (transfer full): A new #LrgRain
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgRain *           lrg_rain_new                        (void);

/* Drop properties */

/**
 * lrg_rain_get_drop_count:
 * @self: A #LrgRain
 *
 * Gets the maximum number of rain drops.
 *
 * Returns: The drop count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_rain_get_drop_count             (LrgRain *self);

/**
 * lrg_rain_set_drop_count:
 * @self: A #LrgRain
 * @count: Maximum number of drops
 *
 * Sets the maximum number of rain drops.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_drop_count             (LrgRain *self,
                                                         guint    count);

/**
 * lrg_rain_get_drop_speed:
 * @self: A #LrgRain
 *
 * Gets the base drop fall speed.
 *
 * Returns: Speed in pixels per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rain_get_drop_speed             (LrgRain *self);

/**
 * lrg_rain_set_drop_speed:
 * @self: A #LrgRain
 * @speed: Speed in pixels per second
 *
 * Sets the base drop fall speed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_drop_speed             (LrgRain *self,
                                                         gfloat   speed);

/**
 * lrg_rain_get_drop_speed_variation:
 * @self: A #LrgRain
 *
 * Gets the speed variation amount.
 *
 * Returns: Speed variation in pixels per second
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rain_get_drop_speed_variation   (LrgRain *self);

/**
 * lrg_rain_set_drop_speed_variation:
 * @self: A #LrgRain
 * @variation: Speed variation in pixels per second
 *
 * Sets the speed variation amount.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_drop_speed_variation   (LrgRain *self,
                                                         gfloat   variation);

/**
 * lrg_rain_get_drop_length:
 * @self: A #LrgRain
 *
 * Gets the rain drop length.
 *
 * Returns: Length in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rain_get_drop_length            (LrgRain *self);

/**
 * lrg_rain_set_drop_length:
 * @self: A #LrgRain
 * @length: Length in pixels
 *
 * Sets the rain drop length.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_drop_length            (LrgRain *self,
                                                         gfloat   length);

/**
 * lrg_rain_get_drop_thickness:
 * @self: A #LrgRain
 *
 * Gets the rain drop thickness.
 *
 * Returns: Thickness in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rain_get_drop_thickness         (LrgRain *self);

/**
 * lrg_rain_set_drop_thickness:
 * @self: A #LrgRain
 * @thickness: Thickness in pixels
 *
 * Sets the rain drop thickness.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_drop_thickness         (LrgRain *self,
                                                         gfloat   thickness);

/* Splash effects */

/**
 * lrg_rain_get_splash_enabled:
 * @self: A #LrgRain
 *
 * Gets whether splash effects are enabled.
 *
 * Returns: %TRUE if splashes are enabled
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_rain_get_splash_enabled         (LrgRain *self);

/**
 * lrg_rain_set_splash_enabled:
 * @self: A #LrgRain
 * @enabled: Whether to enable splashes
 *
 * Sets whether splash effects are enabled.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_splash_enabled         (LrgRain  *self,
                                                         gboolean  enabled);

/**
 * lrg_rain_get_splash_height:
 * @self: A #LrgRain
 *
 * Gets the Y position where splashes occur.
 *
 * Returns: Y position in pixels
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_rain_get_splash_height          (LrgRain *self);

/**
 * lrg_rain_set_splash_height:
 * @self: A #LrgRain
 * @height: Y position in pixels
 *
 * Sets the Y position where splashes occur (ground level).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_splash_height          (LrgRain *self,
                                                         gfloat   height);

/* Appearance */

/**
 * lrg_rain_get_color:
 * @self: A #LrgRain
 * @r: (out) (nullable): Red component
 * @g: (out) (nullable): Green component
 * @b: (out) (nullable): Blue component
 * @a: (out) (nullable): Alpha component
 *
 * Gets the rain color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_get_color                  (LrgRain *self,
                                                         guint8  *r,
                                                         guint8  *g,
                                                         guint8  *b,
                                                         guint8  *a);

/**
 * lrg_rain_set_color:
 * @self: A #LrgRain
 * @r: Red component
 * @g: Green component
 * @b: Blue component
 * @a: Alpha component
 *
 * Sets the rain color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_color                  (LrgRain *self,
                                                         guint8   r,
                                                         guint8   g,
                                                         guint8   b,
                                                         guint8   a);

/* Area */

/**
 * lrg_rain_set_area:
 * @self: A #LrgRain
 * @x: Left position
 * @y: Top position
 * @width: Width
 * @height: Height
 *
 * Sets the area where rain falls.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_set_area                   (LrgRain *self,
                                                         gfloat   x,
                                                         gfloat   y,
                                                         gfloat   width,
                                                         gfloat   height);

/**
 * lrg_rain_get_area:
 * @self: A #LrgRain
 * @x: (out) (nullable): Left position
 * @y: (out) (nullable): Top position
 * @width: (out) (nullable): Width
 * @height: (out) (nullable): Height
 *
 * Gets the area where rain falls.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_rain_get_area                   (LrgRain *self,
                                                         gfloat  *x,
                                                         gfloat  *y,
                                                         gfloat  *width,
                                                         gfloat  *height);

G_END_DECLS
