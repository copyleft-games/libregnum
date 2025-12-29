/* lrg-weather.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Weather state definition.
 *
 * Represents a complete weather state combining multiple effects.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-weather-effect.h"

G_BEGIN_DECLS

#define LRG_TYPE_WEATHER (lrg_weather_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWeather, lrg_weather, LRG, WEATHER, GObject)

struct _LrgWeatherClass
{
    GObjectClass parent_class;

    void (*activate)   (LrgWeather *self);
    void (*deactivate) (LrgWeather *self);
    void (*update)     (LrgWeather *self, gfloat delta_time);

    gpointer _reserved[8];
};

LRG_AVAILABLE_IN_ALL
LrgWeather *        lrg_weather_new                     (const gchar *id,
                                                         const gchar *name);

/* Properties */

LRG_AVAILABLE_IN_ALL
const gchar *       lrg_weather_get_id                  (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
const gchar *       lrg_weather_get_name                (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
gboolean            lrg_weather_is_active               (LrgWeather *self);

/* Effects */

LRG_AVAILABLE_IN_ALL
void                lrg_weather_add_effect              (LrgWeather       *self,
                                                         LrgWeatherEffect *effect);

LRG_AVAILABLE_IN_ALL
gboolean            lrg_weather_remove_effect           (LrgWeather       *self,
                                                         LrgWeatherEffect *effect);

LRG_AVAILABLE_IN_ALL
LrgWeatherEffect *  lrg_weather_get_effect              (LrgWeather  *self,
                                                         const gchar *effect_id);

LRG_AVAILABLE_IN_ALL
GList *             lrg_weather_get_effects             (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
guint               lrg_weather_get_effect_count        (LrgWeather *self);

/* Ambient properties */

LRG_AVAILABLE_IN_ALL
void                lrg_weather_get_ambient_color       (LrgWeather *self,
                                                         guint8     *r,
                                                         guint8     *g,
                                                         guint8     *b);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_set_ambient_color       (LrgWeather *self,
                                                         guint8      r,
                                                         guint8      g,
                                                         guint8      b);

LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_get_ambient_brightness  (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_set_ambient_brightness  (LrgWeather *self,
                                                         gfloat      brightness);

/* Wind */

LRG_AVAILABLE_IN_ALL
void                lrg_weather_get_wind                (LrgWeather *self,
                                                         gfloat     *wind_x,
                                                         gfloat     *wind_y);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_set_wind                (LrgWeather *self,
                                                         gfloat      wind_x,
                                                         gfloat      wind_y);

/* Control */

LRG_AVAILABLE_IN_ALL
void                lrg_weather_activate                (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_deactivate              (LrgWeather *self);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_update                  (LrgWeather *self,
                                                         gfloat      delta_time);

LRG_AVAILABLE_IN_ALL
void                lrg_weather_render                  (LrgWeather *self);

G_END_DECLS
