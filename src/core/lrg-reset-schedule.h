/* lrg-reset-schedule.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Deterministic daily and weekly reset periods for repeatable content.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"

G_BEGIN_DECLS

/**
 * LRG_RESET_SCHEDULE_MIN_UTC_OFFSET:
 *
 * Smallest accepted UTC offset in minutes (UTC-12:00).
 */
#define LRG_RESET_SCHEDULE_MIN_UTC_OFFSET (-720)

/**
 * LRG_RESET_SCHEDULE_MAX_UTC_OFFSET:
 *
 * Largest accepted UTC offset in minutes (UTC+14:00).
 */
#define LRG_RESET_SCHEDULE_MAX_UTC_OFFSET (840)

/**
 * LRG_RESET_SCHEDULE_DAY_SECONDS:
 *
 * Length of a daily reset period in seconds.
 */
#define LRG_RESET_SCHEDULE_DAY_SECONDS (86400)

/**
 * LRG_RESET_SCHEDULE_WEEK_SECONDS:
 *
 * Length of a weekly reset period in seconds.
 */
#define LRG_RESET_SCHEDULE_WEEK_SECONDS (604800)

#define LRG_TYPE_RESET_SCHEDULE (lrg_reset_schedule_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgResetSchedule, lrg_reset_schedule, LRG, RESET_SCHEDULE, GObject)

/**
 * lrg_reset_schedule_new:
 * @reset_hour: local hour of the reset, 0 to 23
 * @weekly_day: weekday of the weekly reset as a #GDateWeekday value, 1 (Monday) to 7 (Sunday)
 * @utc_offset_minutes: fixed offset of the reset clock from UTC in minutes, -720 to 840
 *
 * Creates a reset schedule. The schedule is pure arithmetic over Unix
 * seconds supplied by the caller: it never reads the wall clock and it
 * has no daylight saving rules (the offset is fixed). The reset minute
 * starts at 0 and can be changed with lrg_reset_schedule_set_reset_minute().
 *
 * Returns: (transfer full): a new #LrgResetSchedule
 */
LRG_AVAILABLE_IN_ALL
LrgResetSchedule *lrg_reset_schedule_new                   (guint             reset_hour,
                                                            guint             weekly_day,
                                                            gint              utc_offset_minutes);

/**
 * lrg_reset_schedule_get_reset_hour:
 * @self: an #LrgResetSchedule
 *
 * Returns: the local reset hour, 0 to 23
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_reset_schedule_get_reset_hour        (LrgResetSchedule *self);

/**
 * lrg_reset_schedule_set_reset_hour:
 * @self: an #LrgResetSchedule
 * @reset_hour: local reset hour, 0 to 23
 *
 * Sets the local reset hour. Out-of-range values are programmer errors
 * and leave the schedule unchanged.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_reset_schedule_set_reset_hour        (LrgResetSchedule *self,
                                                            guint             reset_hour);

/**
 * lrg_reset_schedule_get_reset_minute:
 * @self: an #LrgResetSchedule
 *
 * Returns: the local reset minute, 0 to 59
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_reset_schedule_get_reset_minute      (LrgResetSchedule *self);

/**
 * lrg_reset_schedule_set_reset_minute:
 * @self: an #LrgResetSchedule
 * @reset_minute: local reset minute, 0 to 59
 *
 * Sets the local reset minute. Out-of-range values are programmer errors
 * and leave the schedule unchanged.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_reset_schedule_set_reset_minute      (LrgResetSchedule *self,
                                                            guint             reset_minute);

/**
 * lrg_reset_schedule_get_weekly_day:
 * @self: an #LrgResetSchedule
 *
 * Returns: the weekly reset weekday, 1 (Monday) to 7 (Sunday)
 */
LRG_AVAILABLE_IN_ALL
guint             lrg_reset_schedule_get_weekly_day        (LrgResetSchedule *self);

/**
 * lrg_reset_schedule_set_weekly_day:
 * @self: an #LrgResetSchedule
 * @weekly_day: weekday of the weekly reset, 1 (Monday) to 7 (Sunday)
 *
 * Sets the weekly reset weekday. Out-of-range values are programmer
 * errors and leave the schedule unchanged.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_reset_schedule_set_weekly_day        (LrgResetSchedule *self,
                                                            guint             weekly_day);

/**
 * lrg_reset_schedule_get_utc_offset:
 * @self: an #LrgResetSchedule
 *
 * Returns: the fixed UTC offset of the reset clock in minutes
 */
LRG_AVAILABLE_IN_ALL
gint              lrg_reset_schedule_get_utc_offset        (LrgResetSchedule *self);

/**
 * lrg_reset_schedule_set_utc_offset:
 * @self: an #LrgResetSchedule
 * @utc_offset_minutes: fixed UTC offset in minutes, -720 to 840
 *
 * Sets the UTC offset. Out-of-range values are programmer errors and
 * leave the schedule unchanged.
 */
LRG_AVAILABLE_IN_ALL
void              lrg_reset_schedule_set_utc_offset        (LrgResetSchedule *self,
                                                            gint              utc_offset_minutes);

/**
 * lrg_reset_schedule_get_period:
 * @self: an #LrgResetSchedule
 * @period: daily or weekly cadence
 * @now: Unix seconds, may be negative
 *
 * Gets the monotonic index of the reset period containing @now. Period
 * N starts exactly at a reset boundary and ends one second before the
 * next one, so at the reset instant the new period is returned.
 * Consecutive periods differ by one. Floor division is used, so times
 * before the Unix epoch map to negative indices. Index 0 is the period
 * that contains the first boundary at or after the Unix epoch.
 *
 * Returns: the period index
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_reset_schedule_get_period            (LrgResetSchedule *self,
                                                            LrgResetPeriod    period,
                                                            gint64            now);

/**
 * lrg_reset_schedule_get_period_start:
 * @self: an #LrgResetSchedule
 * @period: daily or weekly cadence
 * @index: period index as returned by lrg_reset_schedule_get_period()
 *
 * Gets the Unix second at which period @index begins. The result
 * saturates at %G_MININT64 or %G_MAXINT64 when it cannot be represented.
 *
 * Returns: Unix seconds of the reset boundary that starts @index
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_reset_schedule_get_period_start      (LrgResetSchedule *self,
                                                            LrgResetPeriod    period,
                                                            gint64            index);

/**
 * lrg_reset_schedule_get_next_reset:
 * @self: an #LrgResetSchedule
 * @period: daily or weekly cadence
 * @now: Unix seconds
 *
 * Gets the first reset boundary strictly after @now. At exactly a reset
 * instant this is the following boundary (one full period later).
 * Saturates at %G_MAXINT64.
 *
 * Returns: Unix seconds of the next reset
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_reset_schedule_get_next_reset        (LrgResetSchedule *self,
                                                            LrgResetPeriod    period,
                                                            gint64            now);

/**
 * lrg_reset_schedule_get_seconds_until_reset:
 * @self: an #LrgResetSchedule
 * @period: daily or weekly cadence
 * @now: Unix seconds
 *
 * Gets the number of seconds from @now until the next reset: 1 to 86400
 * for daily periods and 1 to 604800 for weekly periods.
 *
 * Returns: seconds until the next reset
 */
LRG_AVAILABLE_IN_ALL
gint64            lrg_reset_schedule_get_seconds_until_reset (LrgResetSchedule *self,
                                                              LrgResetPeriod    period,
                                                              gint64            now);

/**
 * lrg_reset_schedule_is_same_period:
 * @self: an #LrgResetSchedule
 * @period: daily or weekly cadence
 * @a: Unix seconds
 * @b: Unix seconds
 *
 * Convenience wrapper that compares the period indices of two times.
 *
 * Returns: %TRUE if @a and @b fall in the same reset period
 */
LRG_AVAILABLE_IN_ALL
gboolean          lrg_reset_schedule_is_same_period        (LrgResetSchedule *self,
                                                            LrgResetPeriod    period,
                                                            gint64            a,
                                                            gint64            b);

G_END_DECLS
