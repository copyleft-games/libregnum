/* lrg-snow.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Snow weather effect. In %LRG_WEATHER_SPACE_SCREEN (the default)
 * flakes drift through a pixel area and lrg_weather_effect_render()
 * draws them as dots (plus the accumulated band); in
 * %LRG_WEATHER_SPACE_WORLD they fill a box around the camera in world
 * units, sway on both horizontal axes, ride the wind and
 * lrg_weather_effect_render_world() draws them as camera-facing
 * squares. Speeds, sizes and sway are in the space's units (defaults
 * 100 px/s, 4 px, sway 30 px/s; world 1.2 units/s, 0.05, sway 0.35).
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_SNOW (lrg_snow_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgSnow, lrg_snow, LRG, SNOW, LrgWeatherEffect)

/**
 * lrg_snow_new:
 *
 * Creates a new snow effect.
 *
 * Returns: (transfer full): A new #LrgSnow
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgSnow *           lrg_snow_new                        (void);

/* Flake properties */

LRG_AVAILABLE_IN_ALL
guint               lrg_snow_get_flake_count            (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_flake_count            (LrgSnow *self,
                                                         guint    count);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_flake_speed            (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_flake_speed            (LrgSnow *self,
                                                         gfloat   speed);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_flake_size             (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_flake_size             (LrgSnow *self,
                                                         gfloat   size);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_flake_size_variation   (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_flake_size_variation   (LrgSnow *self,
                                                         gfloat   variation);

/* Drift/sway */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_sway_amount            (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_sway_amount            (LrgSnow *self,
                                                         gfloat   amount);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_sway_speed             (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_sway_speed             (LrgSnow *self,
                                                         gfloat   speed);

/* Accumulation */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_snow_get_accumulation_enabled   (LrgSnow *self);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_accumulation_enabled   (LrgSnow  *self,
                                                         gboolean  enabled);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_snow_get_accumulation_height    (LrgSnow *self);

/* Color and area */

LRG_AVAILABLE_IN_ALL
void                lrg_snow_get_color                  (LrgSnow *self,
                                                         guint8  *r,
                                                         guint8  *g,
                                                         guint8  *b,
                                                         guint8  *a);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_color                  (LrgSnow *self,
                                                         guint8   r,
                                                         guint8   g,
                                                         guint8   b,
                                                         guint8   a);

LRG_AVAILABLE_IN_ALL
void                lrg_snow_set_area                   (LrgSnow *self,
                                                         gfloat   x,
                                                         gfloat   y,
                                                         gfloat   width,
                                                         gfloat   height);

/* Inspection */

/**
 * lrg_snow_get_visible_count:
 * @self: A #LrgSnow
 *
 * The flakes drawn now: live flakes in screen space, flake count x
 * intensity in world space; 0 while inactive.
 *
 * Returns: the number of flakes drawn
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_snow_get_visible_count          (LrgSnow *self);

/**
 * lrg_snow_get_flake:
 * @self: A #LrgSnow
 * @index: flake index
 * @out_position: (out caller-allocates): the flake's centre: (x, y, 0)
 *   in pixels on screen, or its world position folded into the camera
 *   volume
 *
 * Reads where flake @index is drawn. Headless.
 *
 * Returns: %TRUE when that flake is drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_snow_get_flake                  (LrgSnow    *self,
                                                         guint       index,
                                                         GrlVector3 *out_position);

G_END_DECLS
