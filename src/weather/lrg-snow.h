/* lrg-snow.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Snow weather effect.
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

G_END_DECLS
