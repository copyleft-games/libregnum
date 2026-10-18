/* lrg-weather-field.h - a simulated weather map: fronts on the wind
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A grid of cells over a game world holding moisture (0 dry .. 1
 * soaked) and storminess (0 .. 1).  Every fixed step the field is
 * carried along by the wind (semi-Lagrangian advection), relaxes
 * towards each cell's climate (its base moisture), storms die down, and
 * new fronts (blobs of moisture, some of them stormy) form at places
 * and times drawn from a hash of the seed and the step number.  So
 * weather moves across the map instead of being rolled per region,
 * and the same seed and the same total time give the same field
 * bit for bit, however the time was split into advance() calls.
 * Headless; games map moisture and storm to their weather types.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_WEATHER_FIELD (lrg_weather_field_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgWeatherField, lrg_weather_field, LRG, WEATHER_FIELD, GObject)

/**
 * LRG_WEATHER_FIELD_MAX_SIDE:
 *
 * Largest width or height of a field, cells.
 */
#define LRG_WEATHER_FIELD_MAX_SIDE (1024)

/**
 * lrg_weather_field_new:
 * @width: cells along x (1 .. %LRG_WEATHER_FIELD_MAX_SIDE)
 * @height: cells along y (1 .. %LRG_WEATHER_FIELD_MAX_SIDE)
 * @seed: the seed of the fronts
 *
 * A field at base moisture 0.5 everywhere, no storms, no wind, one
 * front a day of radius 6 cells and strength 0.6, relaxing at 4% an
 * hour, stepping every game hour.
 *
 * Returns: (transfer full) (nullable): the field, %NULL for a size out
 *   of range
 */
LRG_AVAILABLE_IN_ALL
LrgWeatherField *   lrg_weather_field_new               (guint            width,
                                                         guint            height,
                                                         guint32          seed);

/**
 * lrg_weather_field_get_width:
 * @self: an #LrgWeatherField
 *
 * Returns: cells along x
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_weather_field_get_width         (LrgWeatherField *self);

/**
 * lrg_weather_field_get_height:
 * @self: an #LrgWeatherField
 *
 * Returns: cells along y
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_weather_field_get_height        (LrgWeatherField *self);

/**
 * lrg_weather_field_get_seed:
 * @self: an #LrgWeatherField
 *
 * Returns: the seed
 */
LRG_AVAILABLE_IN_ALL
guint32             lrg_weather_field_get_seed          (LrgWeatherField *self);

/**
 * lrg_weather_field_set_step:
 * @self: an #LrgWeatherField
 * @seconds: the fixed simulation step (> 0, default 3600)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_step          (LrgWeatherField *self,
                                                         gdouble          seconds);

/**
 * lrg_weather_field_set_wind:
 * @self: an #LrgWeatherField
 * @x: the wind along +x, cells per hour
 * @y: the wind along +y
 *
 * The air the fronts ride on (a game sets the day's wind).
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_wind          (LrgWeatherField *self,
                                                         gdouble          x,
                                                         gdouble          y);

/**
 * lrg_weather_field_get_wind:
 * @self: an #LrgWeatherField
 * @x: (out) (optional): cells per hour along +x
 * @y: (out) (optional): along +y
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_get_wind          (LrgWeatherField *self,
                                                         gdouble         *x,
                                                         gdouble         *y);

/**
 * lrg_weather_field_set_base:
 * @self: an #LrgWeatherField
 * @x: a cell column
 * @y: a cell row
 * @moisture: the cell's climate moisture, 0 .. 1 (clamped)
 *
 * What the cell drifts back to between fronts (dry deserts, wet
 * swamps).  Out-of-range cells are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_base          (LrgWeatherField *self,
                                                         guint            x,
                                                         guint            y,
                                                         gfloat           moisture);

/**
 * lrg_weather_field_get_base:
 * @self: an #LrgWeatherField
 * @x: a cell column
 * @y: a cell row
 *
 * Returns: the cell's base moisture, 0 out of range
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_field_get_base          (LrgWeatherField *self,
                                                         guint            x,
                                                         guint            y);

/**
 * lrg_weather_field_set_relaxation:
 * @self: an #LrgWeatherField
 * @per_hour: the fraction of the way back to the base a cell goes per
 *   hour (0 .. 1), and storms decay twice as fast
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_relaxation    (LrgWeatherField *self,
                                                         gdouble          per_hour);

/**
 * lrg_weather_field_set_fronts:
 * @self: an #LrgWeatherField
 * @per_day: fronts forming per game day over the whole field (>= 0)
 * @radius: a front's radius, cells (> 0)
 * @strength: the moisture a front adds at its centre (0 .. 1)
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_fronts        (LrgWeatherField *self,
                                                         gdouble          per_day,
                                                         gdouble          radius,
                                                         gdouble          strength);

/**
 * lrg_weather_field_reset:
 * @self: an #LrgWeatherField
 *
 * Moisture back to the bases, no storms, step count and time to 0.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_reset             (LrgWeatherField *self);

/**
 * lrg_weather_field_advance:
 * @self: an #LrgWeatherField
 * @seconds: game time that passed
 *
 * Runs every whole step that fits into the time carried over plus
 * @seconds and carries the rest.  Negative and non-finite values are
 * ignored.
 *
 * Returns: the steps run
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_weather_field_advance           (LrgWeatherField *self,
                                                         gdouble          seconds);

/**
 * lrg_weather_field_get_step_count:
 * @self: an #LrgWeatherField
 *
 * Returns: the steps run since the field was made or reset
 */
LRG_AVAILABLE_IN_ALL
guint64             lrg_weather_field_get_step_count    (LrgWeatherField *self);

/**
 * lrg_weather_field_get_moisture:
 * @self: an #LrgWeatherField
 * @x: field x (cell i spans i .. i + 1)
 * @y: field y
 *
 * Bilinear between cell centres, clamped at the edges.
 *
 * Returns: the moisture there, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_field_get_moisture      (LrgWeatherField *self,
                                                         gdouble          x,
                                                         gdouble          y);

/**
 * lrg_weather_field_get_storm:
 * @self: an #LrgWeatherField
 * @x: field x
 * @y: field y
 *
 * Returns: the storminess there, 0 .. 1
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_weather_field_get_storm         (LrgWeatherField *self,
                                                         gdouble          x,
                                                         gdouble          y);

/**
 * lrg_weather_field_set_cell:
 * @self: an #LrgWeatherField
 * @x: a cell column
 * @y: a cell row
 * @moisture: the cell's moisture now (clamped)
 * @storm: its storminess now (clamped)
 *
 * Forces one cell (scripts, tests).  Out-of-range cells are ignored.
 */
LRG_AVAILABLE_IN_ALL
void                lrg_weather_field_set_cell          (LrgWeatherField *self,
                                                         guint            x,
                                                         guint            y,
                                                         gfloat           moisture,
                                                         gfloat           storm);

/**
 * lrg_weather_field_save_state:
 * @self: an #LrgWeatherField
 *
 * Returns: (transfer full): `a{sv}`: "width", "height", "seed" (`u`),
 *   "steps" (`t`), "carry" (`d`), "wind" (`(dd)`), "moisture" and
 *   "storm" (`ad`, row by row)
 */
LRG_AVAILABLE_IN_ALL
GVariant *          lrg_weather_field_save_state        (LrgWeatherField *self);

/**
 * lrg_weather_field_restore_state:
 * @self: an #LrgWeatherField
 * @state: a state from lrg_weather_field_save_state()
 * @error: return location for a #GError
 *
 * The size must match the field's; the seed is taken from @state.
 * Everything is validated before anything is applied.
 *
 * Returns: %TRUE on success; %FALSE with %G_IO_ERROR_INVALID_DATA for a
 *   malformed state or another size
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_weather_field_restore_state     (LrgWeatherField *self,
                                                         GVariant        *state,
                                                         GError         **error);

G_END_DECLS
