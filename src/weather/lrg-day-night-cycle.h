/* lrg-day-night-cycle.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Day/night cycle manager.
 *
 * Manages time-of-day progression and provides lighting/color information
 * for different times of day.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_DAY_NIGHT_CYCLE (lrg_day_night_cycle_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDayNightCycle, lrg_day_night_cycle, LRG, DAY_NIGHT_CYCLE, GObject)

/**
 * LrgTimeOfDay:
 * @LRG_TIME_OF_DAY_DAWN: Early morning (sunrise)
 * @LRG_TIME_OF_DAY_MORNING: Morning hours
 * @LRG_TIME_OF_DAY_NOON: Midday
 * @LRG_TIME_OF_DAY_AFTERNOON: Afternoon hours
 * @LRG_TIME_OF_DAY_DUSK: Evening (sunset)
 * @LRG_TIME_OF_DAY_NIGHT: Nighttime
 *
 * Time of day periods.
 */
typedef enum
{
    LRG_TIME_OF_DAY_DAWN,
    LRG_TIME_OF_DAY_MORNING,
    LRG_TIME_OF_DAY_NOON,
    LRG_TIME_OF_DAY_AFTERNOON,
    LRG_TIME_OF_DAY_DUSK,
    LRG_TIME_OF_DAY_NIGHT,
} LrgTimeOfDay;

/**
 * lrg_day_night_cycle_new:
 *
 * Creates a new day/night cycle manager.
 *
 * Returns: (transfer full): A new #LrgDayNightCycle
 */
LRG_AVAILABLE_IN_ALL
LrgDayNightCycle * lrg_day_night_cycle_new (void);

/* Time control */

/**
 * lrg_day_night_cycle_get_time:
 * @self: an #LrgDayNightCycle
 *
 * Gets the current time as a normalized value (0.0 = midnight, 0.5 = noon).
 *
 * Returns: Current time (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_day_night_cycle_get_time (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_set_time:
 * @self: an #LrgDayNightCycle
 * @time: time value (0.0 to 1.0, wraps)
 *
 * Sets the current time directly.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_time (LrgDayNightCycle *self,
                                   gfloat            time);

/**
 * lrg_day_night_cycle_get_time_of_day:
 * @self: an #LrgDayNightCycle
 *
 * Gets the current time-of-day period.
 *
 * Returns: Current #LrgTimeOfDay
 */
LRG_AVAILABLE_IN_ALL
LrgTimeOfDay lrg_day_night_cycle_get_time_of_day (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_get_hours:
 * @self: an #LrgDayNightCycle
 *
 * Gets the current time as hours (0-24).
 *
 * Returns: Current hour (0.0 to 24.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_day_night_cycle_get_hours (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_set_hours:
 * @self: an #LrgDayNightCycle
 * @hours: hour value (0.0 to 24.0)
 *
 * Sets the current time in hours.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_hours (LrgDayNightCycle *self,
                                    gfloat            hours);

/* Cycle speed */

/**
 * lrg_day_night_cycle_get_day_length:
 * @self: an #LrgDayNightCycle
 *
 * Gets the length of a full day in real seconds.
 *
 * Returns: Day length in seconds
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_day_night_cycle_get_day_length (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_set_day_length:
 * @self: an #LrgDayNightCycle
 * @seconds: length of a full day in real seconds
 *
 * Sets how long a full day takes in real time.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_day_length (LrgDayNightCycle *self,
                                         gfloat            seconds);

/**
 * lrg_day_night_cycle_get_paused:
 * @self: an #LrgDayNightCycle
 *
 * Gets whether the cycle is paused.
 *
 * Returns: %TRUE if paused
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_day_night_cycle_get_paused (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_set_paused:
 * @self: an #LrgDayNightCycle
 * @paused: whether to pause the cycle
 *
 * Pauses or resumes the day/night cycle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_paused (LrgDayNightCycle *self,
                                     gboolean          paused);

/* Ambient properties */

/**
 * lrg_day_night_cycle_get_ambient_color:
 * @self: an #LrgDayNightCycle
 * @r: (out) (nullable): red component (0-255)
 * @g: (out) (nullable): green component (0-255)
 * @b: (out) (nullable): blue component (0-255)
 *
 * Gets the current ambient light color based on time of day.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_get_ambient_color (LrgDayNightCycle *self,
                                            guint8           *r,
                                            guint8           *g,
                                            guint8           *b);

/**
 * lrg_day_night_cycle_get_ambient_brightness:
 * @self: an #LrgDayNightCycle
 *
 * Gets the current ambient brightness based on time of day.
 *
 * Returns: Brightness multiplier (0.0 to 1.0)
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_day_night_cycle_get_ambient_brightness (LrgDayNightCycle *self);

/**
 * lrg_day_night_cycle_get_sun_angle:
 * @self: an #LrgDayNightCycle
 *
 * Gets the current sun angle in degrees (0 = horizon east, 90 = overhead, 180 = horizon west).
 *
 * Returns: Sun angle in degrees
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_day_night_cycle_get_sun_angle (LrgDayNightCycle *self);

/* Color configuration */

/**
 * lrg_day_night_cycle_set_dawn_color:
 * @self: an #LrgDayNightCycle
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 *
 * Sets the ambient color for dawn.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_dawn_color (LrgDayNightCycle *self,
                                         guint8            r,
                                         guint8            g,
                                         guint8            b);

/**
 * lrg_day_night_cycle_set_day_color:
 * @self: an #LrgDayNightCycle
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 *
 * Sets the ambient color for daytime.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_day_color (LrgDayNightCycle *self,
                                        guint8            r,
                                        guint8            g,
                                        guint8            b);

/**
 * lrg_day_night_cycle_set_dusk_color:
 * @self: an #LrgDayNightCycle
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 *
 * Sets the ambient color for dusk.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_dusk_color (LrgDayNightCycle *self,
                                         guint8            r,
                                         guint8            g,
                                         guint8            b);

/**
 * lrg_day_night_cycle_set_night_color:
 * @self: an #LrgDayNightCycle
 * @r: red component (0-255)
 * @g: green component (0-255)
 * @b: blue component (0-255)
 *
 * Sets the ambient color for nighttime.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_set_night_color (LrgDayNightCycle *self,
                                          guint8            r,
                                          guint8            g,
                                          guint8            b);

/* Update */

/**
 * lrg_day_night_cycle_update:
 * @self: an #LrgDayNightCycle
 * @delta_time: time elapsed in seconds
 *
 * Updates the day/night cycle.
 */
LRG_AVAILABLE_IN_ALL
void lrg_day_night_cycle_update (LrgDayNightCycle *self,
                                 gfloat            delta_time);

G_END_DECLS
