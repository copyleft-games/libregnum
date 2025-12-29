/* lrg-fog.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Fog weather effect.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-weather-effect.h"

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

G_END_DECLS
