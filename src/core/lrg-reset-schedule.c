/* lrg-reset-schedule.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Deterministic daily and weekly reset periods.
 *
 * Every calculation reduces to "which multiple of the period length
 * does this Unix time fall into, after shifting by the phase of the
 * first reset boundary". The phase is the Unix second of the first
 * reset boundary at or after the epoch, normalised into [0, length).
 * Division always floors (towards negative infinity) so times before
 * 1970 behave exactly like times after it.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "core/lrg-reset-schedule.h"

/* The Unix epoch (day 0) was a Thursday, GDateWeekday 4. */
#define LRG_RESET_EPOCH_WEEKDAY (4)

struct _LrgResetSchedule
{
    GObject parent_instance;

    guint   reset_hour;
    guint   reset_minute;
    guint   weekly_day;
    gint    utc_offset;
};

G_DEFINE_FINAL_TYPE (LrgResetSchedule, lrg_reset_schedule, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_RESET_HOUR,
    PROP_RESET_MINUTE,
    PROP_WEEKLY_DAY,
    PROP_UTC_OFFSET,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/*
 * floor_div_mod:
 * @value: dividend
 * @divisor: strictly positive divisor
 * @out_rem: (out): remainder in [0, divisor)
 *
 * Floor division that never overflows for positive divisors.
 */
static gint64
floor_div_mod (gint64  value,
               gint64  divisor,
               gint64 *out_rem)
{
    gint64 q;
    gint64 r;

    q = value / divisor;
    r = value % divisor;
    if (r < 0)
    {
        r += divisor;
        q -= 1;
    }
    *out_rem = r;
    return q;
}

/*
 * period_length:
 * @period: cadence
 *
 * Returns: the cadence length in seconds
 */
static gint64
period_length (LrgResetPeriod period)
{
    if (period == LRG_RESET_PERIOD_WEEKLY)
        return (gint64)LRG_RESET_SCHEDULE_WEEK_SECONDS;
    return (gint64)LRG_RESET_SCHEDULE_DAY_SECONDS;
}

/*
 * period_phase:
 * @self: schedule
 * @period: cadence
 *
 * Computes the Unix second of the first reset boundary at or after the
 * epoch. A boundary happens when local time (UTC plus the offset) equals
 * the reset time of day; for weekly periods the local calendar day must
 * additionally be the configured weekday.
 *
 * Returns: phase in [0, period length)
 */
static gint64
period_phase (LrgResetSchedule *self,
              LrgResetPeriod    period)
{
    gint64 length;
    gint64 phase;
    gint64 rem;
    gint64 day_shift;

    length = period_length (period);

    /* UTC second of a local reset on local day 0 (1970-01-01 local). */
    phase = (gint64)self->reset_hour * 3600
          + (gint64)self->reset_minute * 60
          - (gint64)self->utc_offset * 60;

    if (period == LRG_RESET_PERIOD_WEEKLY)
    {
        /* Days from local day 0 (Thursday) to the first configured weekday. */
        day_shift = ((gint64)self->weekly_day - LRG_RESET_EPOCH_WEEKDAY + 7) % 7;
        phase += day_shift * (gint64)LRG_RESET_SCHEDULE_DAY_SECONDS;
    }

    (void)floor_div_mod (phase, length, &rem);
    return rem;
}

static void
lrg_reset_schedule_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    LrgResetSchedule *self = LRG_RESET_SCHEDULE (object);

    switch (prop_id)
    {
    case PROP_RESET_HOUR:
        g_value_set_uint (value, self->reset_hour);
        break;
    case PROP_RESET_MINUTE:
        g_value_set_uint (value, self->reset_minute);
        break;
    case PROP_WEEKLY_DAY:
        g_value_set_uint (value, self->weekly_day);
        break;
    case PROP_UTC_OFFSET:
        g_value_set_int (value, self->utc_offset);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reset_schedule_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    LrgResetSchedule *self = LRG_RESET_SCHEDULE (object);

    switch (prop_id)
    {
    case PROP_RESET_HOUR:
        lrg_reset_schedule_set_reset_hour (self, g_value_get_uint (value));
        break;
    case PROP_RESET_MINUTE:
        lrg_reset_schedule_set_reset_minute (self, g_value_get_uint (value));
        break;
    case PROP_WEEKLY_DAY:
        lrg_reset_schedule_set_weekly_day (self, g_value_get_uint (value));
        break;
    case PROP_UTC_OFFSET:
        lrg_reset_schedule_set_utc_offset (self, g_value_get_int (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_reset_schedule_class_init (LrgResetScheduleClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_reset_schedule_get_property;
    object_class->set_property = lrg_reset_schedule_set_property;

    /**
     * LrgResetSchedule:reset-hour:
     *
     * Local hour (0-23) at which daily and weekly periods reset.
     */
    properties[PROP_RESET_HOUR] =
        g_param_spec_uint ("reset-hour", "Reset Hour", "Local reset hour",
                           0, 23, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgResetSchedule:reset-minute:
     *
     * Local minute (0-59) at which daily and weekly periods reset.
     */
    properties[PROP_RESET_MINUTE] =
        g_param_spec_uint ("reset-minute", "Reset Minute", "Local reset minute",
                           0, 59, 0,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgResetSchedule:weekly-day:
     *
     * Weekday of the weekly reset as a #GDateWeekday value (1 = Monday,
     * 7 = Sunday). Defaults to 2 (Tuesday).
     */
    properties[PROP_WEEKLY_DAY] =
        g_param_spec_uint ("weekly-day", "Weekly Day", "Weekday of the weekly reset",
                           1, 7, 2,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    /**
     * LrgResetSchedule:utc-offset:
     *
     * Fixed offset of the reset clock from UTC, in minutes.
     */
    properties[PROP_UTC_OFFSET] =
        g_param_spec_int ("utc-offset", "UTC Offset", "Reset clock offset from UTC in minutes",
                          LRG_RESET_SCHEDULE_MIN_UTC_OFFSET,
                          LRG_RESET_SCHEDULE_MAX_UTC_OFFSET, 0,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_reset_schedule_init (LrgResetSchedule *self)
{
    self->reset_hour = 0;
    self->reset_minute = 0;
    self->weekly_day = G_DATE_TUESDAY;
    self->utc_offset = 0;
}

LrgResetSchedule *
lrg_reset_schedule_new (guint reset_hour,
                        guint weekly_day,
                        gint  utc_offset_minutes)
{
    g_return_val_if_fail (reset_hour <= 23, NULL);
    g_return_val_if_fail (weekly_day >= 1 && weekly_day <= 7, NULL);
    g_return_val_if_fail (utc_offset_minutes >= LRG_RESET_SCHEDULE_MIN_UTC_OFFSET &&
                          utc_offset_minutes <= LRG_RESET_SCHEDULE_MAX_UTC_OFFSET, NULL);

    return g_object_new (LRG_TYPE_RESET_SCHEDULE,
                         "reset-hour", reset_hour,
                         "weekly-day", weekly_day,
                         "utc-offset", utc_offset_minutes,
                         NULL);
}

guint
lrg_reset_schedule_get_reset_hour (LrgResetSchedule *self)
{
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    return self->reset_hour;
}

void
lrg_reset_schedule_set_reset_hour (LrgResetSchedule *self,
                                   guint             reset_hour)
{
    g_return_if_fail (LRG_IS_RESET_SCHEDULE (self));
    g_return_if_fail (reset_hour <= 23);

    if (self->reset_hour == reset_hour)
        return;
    self->reset_hour = reset_hour;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESET_HOUR]);
}

guint
lrg_reset_schedule_get_reset_minute (LrgResetSchedule *self)
{
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    return self->reset_minute;
}

void
lrg_reset_schedule_set_reset_minute (LrgResetSchedule *self,
                                     guint             reset_minute)
{
    g_return_if_fail (LRG_IS_RESET_SCHEDULE (self));
    g_return_if_fail (reset_minute <= 59);

    if (self->reset_minute == reset_minute)
        return;
    self->reset_minute = reset_minute;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESET_MINUTE]);
}

guint
lrg_reset_schedule_get_weekly_day (LrgResetSchedule *self)
{
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), G_DATE_TUESDAY);
    return self->weekly_day;
}

void
lrg_reset_schedule_set_weekly_day (LrgResetSchedule *self,
                                   guint             weekly_day)
{
    g_return_if_fail (LRG_IS_RESET_SCHEDULE (self));
    g_return_if_fail (weekly_day >= 1 && weekly_day <= 7);

    if (self->weekly_day == weekly_day)
        return;
    self->weekly_day = weekly_day;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WEEKLY_DAY]);
}

gint
lrg_reset_schedule_get_utc_offset (LrgResetSchedule *self)
{
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    return self->utc_offset;
}

void
lrg_reset_schedule_set_utc_offset (LrgResetSchedule *self,
                                   gint              utc_offset_minutes)
{
    g_return_if_fail (LRG_IS_RESET_SCHEDULE (self));
    g_return_if_fail (utc_offset_minutes >= LRG_RESET_SCHEDULE_MIN_UTC_OFFSET &&
                      utc_offset_minutes <= LRG_RESET_SCHEDULE_MAX_UTC_OFFSET);

    if (self->utc_offset == utc_offset_minutes)
        return;
    self->utc_offset = utc_offset_minutes;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UTC_OFFSET]);
}

gint64
lrg_reset_schedule_get_period (LrgResetSchedule *self,
                               LrgResetPeriod    period,
                               gint64            now)
{
    gint64 length;
    gint64 phase;
    gint64 quotient;
    gint64 rem;

    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, 0);

    length = period_length (period);
    phase = period_phase (self, period);

    /*
     * floor((now - phase) / length) computed without forming now - phase,
     * which could overflow near G_MININT64. rem is in [0, length) and
     * phase is in [0, length), so rem - phase is in (-length, length).
     */
    quotient = floor_div_mod (now, length, &rem);
    if (rem < phase)
        quotient -= 1;
    return quotient;
}

gint64
lrg_reset_schedule_get_period_start (LrgResetSchedule *self,
                                     LrgResetPeriod    period,
                                     gint64            index)
{
    gint64 length;
    gint64 phase;

    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, 0);

    length = period_length (period);
    phase = period_phase (self, period);

    /* Saturate instead of overflowing: index * length + phase. */
    if (index > (G_MAXINT64 - phase) / length)
        return G_MAXINT64;

    /*
     * lowest is the smallest index whose product is representable
     * (G_MININT64 / length truncates towards zero, i.e. rounds up).
     * One index lower the product alone underflows, but adding the
     * phase may bring the start back into range: evaluate it as
     * lowest * length - (length - phase) with an explicit bound check.
     */
    {
        gint64 lowest = G_MININT64 / length;

        if (index >= lowest)
            return index * length + phase;
        if (index == lowest - 1 &&
            lowest * length >= G_MININT64 + (length - phase))
            return lowest * length - (length - phase);
        return G_MININT64;
    }
}

gint64
lrg_reset_schedule_get_next_reset (LrgResetSchedule *self,
                                   LrgResetPeriod    period,
                                   gint64            now)
{
    gint64 index;

    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, 0);

    /* The index is at most G_MAXINT64 / 86400, so + 1 cannot overflow. */
    index = lrg_reset_schedule_get_period (self, period, now);
    return lrg_reset_schedule_get_period_start (self, period, index + 1);
}

gint64
lrg_reset_schedule_get_seconds_until_reset (LrgResetSchedule *self,
                                            LrgResetPeriod    period,
                                            gint64            now)
{
    gint64 next;

    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), 0);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, 0);

    next = lrg_reset_schedule_get_next_reset (self, period, now);
    return next - now;
}

gboolean
lrg_reset_schedule_is_same_period (LrgResetSchedule *self,
                                   LrgResetPeriod    period,
                                   gint64            a,
                                   gint64            b)
{
    g_return_val_if_fail (LRG_IS_RESET_SCHEDULE (self), FALSE);
    g_return_val_if_fail (period == LRG_RESET_PERIOD_DAILY ||
                          period == LRG_RESET_PERIOD_WEEKLY, FALSE);

    return lrg_reset_schedule_get_period (self, period, a) ==
           lrg_reset_schedule_get_period (self, period, b);
}
