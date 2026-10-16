/* lrg-fog.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fog weather effect: distance fog parameters for shaders
 * (lrg_fog_apply() feeds an #LrgLightSet) and a screen-veil fallback.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-weather-effect.h"
#include "../graphics/lrg-light-set.h"

G_BEGIN_DECLS

#define LRG_TYPE_FOG (lrg_fog_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgFog, lrg_fog, LRG, FOG, LrgWeatherEffect)

/**
 * lrg_fog_new:
 *
 * Creates a new fog effect.
 *
 * Returns: (transfer full): A new #LrgFog
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgFog *            lrg_fog_new                         (void);

/* Fog type */

LRG_AVAILABLE_IN_ALL
LrgFogType          lrg_fog_get_fog_type                (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_fog_type                (LrgFog    *self,
                                                         LrgFogType type);

/* Density */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_density                 (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_density                 (LrgFog *self,
                                                         gfloat  density);

/* Distance (for linear fog) */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_start_distance          (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_start_distance          (LrgFog *self,
                                                         gfloat  distance);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_end_distance            (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_end_distance            (LrgFog *self,
                                                         gfloat  distance);

/* Height fog */

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_height_falloff          (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_height_falloff          (LrgFog *self,
                                                         gfloat  falloff);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_base_height             (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_base_height             (LrgFog *self,
                                                         gfloat  height);

/* Color */

LRG_AVAILABLE_IN_ALL
void                lrg_fog_get_color                   (LrgFog *self,
                                                         guint8 *r,
                                                         guint8 *g,
                                                         guint8 *b,
                                                         guint8 *a);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_color                   (LrgFog *self,
                                                         guint8  r,
                                                         guint8  g,
                                                         guint8  b,
                                                         guint8  a);

/* Animation */

LRG_AVAILABLE_IN_ALL
gboolean            lrg_fog_get_animated                (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_animated                (LrgFog   *self,
                                                         gboolean  animated);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_scroll_speed_x          (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_scroll_speed_x          (LrgFog *self,
                                                         gfloat  speed);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_scroll_speed_y          (LrgFog *self);

LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_scroll_speed_y          (LrgFog *self,
                                                         gfloat  speed);

/* Screen veil */

/**
 * lrg_fog_get_veil_opacity:
 * @self: A #LrgFog
 *
 * Returns: the veil strength, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_veil_opacity            (LrgFog *self);

/**
 * lrg_fog_set_veil_opacity:
 * @self: A #LrgFog
 * @opacity: 0 .. 1 (default 0.5)
 *
 * Sets the strength of the screen veil that lrg_weather_effect_render()
 * (2D) and lrg_weather_effect_render_overlay() (3D) draw: a
 * full-screen rectangle of the fog colour with alpha = colour alpha x
 * intensity x @opacity. A 3D scene that fogs per fragment through
 * lrg_fog_apply() sets 0, or keeps a little for atmosphere.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fog_set_veil_opacity            (LrgFog *self,
                                                         gfloat  opacity);

/**
 * lrg_fog_get_scroll_offset:
 * @self: A #LrgFog
 * @x: (out) (optional): accumulated X scroll
 * @y: (out) (optional): accumulated Y scroll
 *
 * The animated offset (scroll speeds plus a tenth of the wind,
 * integrated by update) for shaders that texture their fog.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_fog_get_scroll_offset           (LrgFog *self,
                                                         gfloat *x,
                                                         gfloat *y);

/* Distance fog */

/**
 * lrg_fog_get_amount:
 * @self: A #LrgFog
 * @distance: distance from the eye, world units
 * @height: world height of the point (for %LRG_FOG_TYPE_HEIGHT)
 *
 * The share of the fog colour a point gets now, exactly what a
 * #LrgLightSet given this fog by lrg_fog_apply() computes per
 * fragment: the fog type's model (see lrg_light_set_set_fog()) with
 * this effect's density, start/end distances, base height and falloff,
 * times the colour alpha and the intensity. 0 while inactive.
 *
 * Returns: 0 (clear) .. 1 (only fog colour)
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_fog_get_amount                  (LrgFog *self,
                                                         gfloat  distance,
                                                         gfloat  height);

/**
 * lrg_fog_apply:
 * @self: A #LrgFog
 * @lights: the #LrgLightSet the scene draws with
 *
 * Hands this fog to @lights (lrg_light_set_set_fog() and
 * lrg_light_set_set_fog_height()), the colour's alpha scaled by the
 * intensity so manager transitions fade it in and out. While the
 * effect is inactive or at zero intensity the light set's fog is
 * cleared. Call each frame before lrg_light_set_apply(); see also
 * lrg_weather_manager_apply_fog().
 *
 * Returns: %TRUE when fog was set, %FALSE when it was cleared
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_fog_apply                       (LrgFog      *self,
                                                         LrgLightSet *lights);

G_END_DECLS
