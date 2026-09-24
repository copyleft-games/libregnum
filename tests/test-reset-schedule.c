/* test-reset-schedule.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for LrgResetSchedule.
 *
 * Boundaries are checked against hand-computed Unix times and against an
 * independent GDateTime oracle: for many deterministic pseudo-random
 * times the period start returned by the schedule must contain the time
 * and must fall exactly on the configured local reset time and weekday.
 */

#include <glib.h>
#include <glib-object.h>

#include "lrg-enums.h"
#include "core/lrg-reset-schedule.h"

#define DAY  ((gint64)86400)
#define WEEK ((gint64)604800)

/* ========================================================================== */
/*                                  Helpers                                   */
/* ========================================================================== */

/*
 * unix_utc:
 *
 * Returns: Unix seconds of a UTC calendar time, computed with GDateTime.
 */
static gint64
unix_utc (gint year,
          gint month,
          gint day,
          gint hour,
          gint minute,
          gint second)
{
    g_autoptr(GDateTime) dt = NULL;

    dt = g_date_time_new_utc (year, month, day, hour, minute, (gdouble)second);
    g_assert_nonnull (dt);
    return g_date_time_to_unix (dt);
}

/*
 * assert_boundary:
 *
 * Checks with GDateTime that @start is a reset boundary of @schedule.
 */
static void
assert_boundary (LrgResetSchedule *schedule,
                 LrgResetPeriod    period,
                 gint64            start)
{
    g_autoptr(GDateTime) local = NULL;
    gint                 offset;

    offset = lrg_reset_schedule_get_utc_offset (schedule);
    local = g_date_time_new_from_unix_utc (start + (gint64)offset * 60);
    g_assert_nonnull (local);

    g_assert_cmpint (g_date_time_get_hour (local), ==,
                     (gint)lrg_reset_schedule_get_reset_hour (schedule));
    g_assert_cmpint (g_date_time_get_minute (local), ==,
                     (gint)lrg_reset_schedule_get_reset_minute (schedule));
    g_assert_cmpint (g_date_time_get_second (local), ==, 0);
    if (period == LRG_RESET_PERIOD_WEEKLY)
        g_assert_cmpint (g_date_time_get_day_of_week (local), ==,
                         (gint)lrg_reset_schedule_get_weekly_day (schedule));
}

/*
 * assert_period_contract:
 *
 * Checks every documented relation between the four query functions at @t.
 */
static void
assert_period_contract (LrgResetSchedule *schedule,
                        LrgResetPeriod    period,
                        gint64            t)
{
    gint64 length;
    gint64 index;
    gint64 start;
    gint64 next;
    gint64 until;

    length = (period == LRG_RESET_PERIOD_WEEKLY) ? WEEK : DAY;
    index = lrg_reset_schedule_get_period (schedule, period, t);
    start = lrg_reset_schedule_get_period_start (schedule, period, index);
    next = lrg_reset_schedule_get_next_reset (schedule, period, t);
    until = lrg_reset_schedule_get_seconds_until_reset (schedule, period, t);

    /* The period contains t and has the full length. */
    g_assert_cmpint (start, <=, t);
    g_assert_cmpint (t, <, start + length);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, period, index + 1), ==,
                     start + length);

    /* The next reset is strictly later and at most one period away. */
    g_assert_cmpint (next, ==, start + length);
    g_assert_cmpint (until, ==, next - t);
    g_assert_cmpint (until, >=, 1);
    g_assert_cmpint (until, <=, length);

    /* Boundary instants belong to the new period; one second earlier is the old. */
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, period, start), ==, index);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, period, start - 1), ==, index - 1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, period, next), ==, index + 1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, period, next - 1), ==, index);

    assert_boundary (schedule, period, start);
}

/* ========================================================================== */
/*                          Construction / properties                         */
/* ========================================================================== */

static void
test_defaults (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;

    schedule = g_object_new (LRG_TYPE_RESET_SCHEDULE, NULL);
    g_assert_cmpuint (lrg_reset_schedule_get_reset_hour (schedule), ==, 0);
    g_assert_cmpuint (lrg_reset_schedule_get_reset_minute (schedule), ==, 0);
    g_assert_cmpuint (lrg_reset_schedule_get_weekly_day (schedule), ==, G_DATE_TUESDAY);
    g_assert_cmpint (lrg_reset_schedule_get_utc_offset (schedule), ==, 0);
}

static void
test_new (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;

    schedule = lrg_reset_schedule_new (15, G_DATE_WEDNESDAY, -480);
    g_assert_nonnull (schedule);
    g_assert_cmpuint (lrg_reset_schedule_get_reset_hour (schedule), ==, 15);
    g_assert_cmpuint (lrg_reset_schedule_get_reset_minute (schedule), ==, 0);
    g_assert_cmpuint (lrg_reset_schedule_get_weekly_day (schedule), ==, G_DATE_WEDNESDAY);
    g_assert_cmpint (lrg_reset_schedule_get_utc_offset (schedule), ==, -480);
}

static void
count_notify (GObject    *object,
              GParamSpec *pspec,
              gpointer    user_data)
{
    guint *count = user_data;

    (*count)++;
    (void)object;
    (void)pspec;
}

static void
test_properties (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    guint hour;
    guint minute;
    guint day;
    gint  offset;
    guint notifications;

    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 0);
    notifications = 0;
    g_signal_connect (schedule, "notify", G_CALLBACK (count_notify), &notifications);

    g_object_set (schedule,
                  "reset-hour", 6,
                  "reset-minute", 30,
                  "weekly-day", 7,
                  "utc-offset", 330,
                  NULL);
    g_object_get (schedule,
                  "reset-hour", &hour,
                  "reset-minute", &minute,
                  "weekly-day", &day,
                  "utc-offset", &offset,
                  NULL);
    g_assert_cmpuint (hour, ==, 6);
    g_assert_cmpuint (minute, ==, 30);
    g_assert_cmpuint (day, ==, 7);
    g_assert_cmpint (offset, ==, 330);
    g_assert_cmpuint (notifications, ==, 4);

    /* Setting identical values does not notify. */
    lrg_reset_schedule_set_reset_hour (schedule, 6);
    lrg_reset_schedule_set_reset_minute (schedule, 30);
    lrg_reset_schedule_set_weekly_day (schedule, 7);
    lrg_reset_schedule_set_utc_offset (schedule, 330);
    g_assert_cmpuint (notifications, ==, 4);

    /* Extremes are accepted. */
    lrg_reset_schedule_set_reset_hour (schedule, 23);
    lrg_reset_schedule_set_reset_minute (schedule, 59);
    lrg_reset_schedule_set_weekly_day (schedule, 1);
    lrg_reset_schedule_set_utc_offset (schedule, -720);
    g_assert_cmpint (lrg_reset_schedule_get_utc_offset (schedule), ==, -720);
    lrg_reset_schedule_set_utc_offset (schedule, 840);
    g_assert_cmpint (lrg_reset_schedule_get_utc_offset (schedule), ==, 840);
    g_assert_cmpuint (notifications, ==, 9);
}

static void
test_setters_reject_out_of_range (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;

    schedule = lrg_reset_schedule_new (4, G_DATE_TUESDAY, 60);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*reset_hour <= 23*");
    lrg_reset_schedule_set_reset_hour (schedule, 24);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*reset_minute <= 59*");
    lrg_reset_schedule_set_reset_minute (schedule, 60);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*weekly_day*");
    lrg_reset_schedule_set_weekly_day (schedule, 0);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*weekly_day*");
    lrg_reset_schedule_set_weekly_day (schedule, 8);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*utc_offset_minutes*");
    lrg_reset_schedule_set_utc_offset (schedule, -721);
    g_test_assert_expected_messages ();

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*utc_offset_minutes*");
    lrg_reset_schedule_set_utc_offset (schedule, 841);
    g_test_assert_expected_messages ();

    /* State is unchanged by every rejected call. */
    g_assert_cmpuint (lrg_reset_schedule_get_reset_hour (schedule), ==, 4);
    g_assert_cmpuint (lrg_reset_schedule_get_reset_minute (schedule), ==, 0);
    g_assert_cmpuint (lrg_reset_schedule_get_weekly_day (schedule), ==, G_DATE_TUESDAY);
    g_assert_cmpint (lrg_reset_schedule_get_utc_offset (schedule), ==, 60);
}

static void
test_new_rejects_out_of_range (void)
{
    LrgResetSchedule *schedule;

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*reset_hour <= 23*");
    schedule = lrg_reset_schedule_new (24, 2, 0);
    g_test_assert_expected_messages ();
    g_assert_null (schedule);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*weekly_day*");
    schedule = lrg_reset_schedule_new (0, 0, 0);
    g_test_assert_expected_messages ();
    g_assert_null (schedule);

    g_test_expect_message (NULL, G_LOG_LEVEL_CRITICAL, "*utc_offset_minutes*");
    schedule = lrg_reset_schedule_new (0, 2, 900);
    g_test_assert_expected_messages ();
    g_assert_null (schedule);
}

/* ========================================================================== */
/*                               Daily periods                                */
/* ========================================================================== */

static void
test_daily_midnight_utc (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_DAILY;

    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 0);

    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 0), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 1), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, DAY - 1), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, DAY), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, DAY + 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 0), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 1), ==, DAY);

    /* Exactly at a reset the next reset is one full day later. */
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, 0), ==, DAY);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, 0), ==, DAY);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, DAY - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, 1), ==, DAY - 1);
}

static void
test_daily_negative_times (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_DAILY;

    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 0);

    /* Floor division: the second before the epoch is period -1. */
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, -1), ==, -1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, -DAY), ==, -1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, -DAY - 1), ==, -2);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, -1), ==, -DAY);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, -2), ==, -2 * DAY);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, -1), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, -DAY), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, -1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, -DAY), ==, DAY);

    /* 1960-06-15 12:00 UTC lies in the period that started at midnight. */
    {
        gint64 t = unix_utc (1960, 6, 15, 12, 0, 0);
        gint64 idx = lrg_reset_schedule_get_period (schedule, p, t);
        g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, idx), ==,
                         unix_utc (1960, 6, 15, 0, 0, 0));
        assert_period_contract (schedule, p, t);
    }
}

static void
test_daily_reset_hour_and_minute (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_DAILY;
    gint64 reset;

    /* 04:30 UTC reset. */
    schedule = lrg_reset_schedule_new (4, G_DATE_TUESDAY, 0);
    lrg_reset_schedule_set_reset_minute (schedule, 30);
    reset = unix_utc (2026, 9, 24, 4, 30, 0);

    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset + 1), ==,
                     lrg_reset_schedule_get_period (schedule, p, reset));
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset), ==, reset + DAY);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset + 1), ==, reset + DAY);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, reset - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, reset), ==, DAY);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, reset + 1), ==, DAY - 1);

    /* Period 0 starts at the first boundary at or after the epoch. */
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 0), ==, 4 * 3600 + 30 * 60);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 0), ==, -1);
}

static void
test_daily_positive_offset (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_DAILY;
    gint64 reset;

    /* Midnight in UTC+01:00 is 23:00 UTC the previous day. */
    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 60);
    reset = unix_utc (2026, 3, 1, 23, 0, 0);

    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset), ==, reset + DAY);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 0), ==, DAY - 3600);

    /* UTC+14:00 (840 minutes), reset at 05:00 local = 15:00 UTC previous day. */
    lrg_reset_schedule_set_utc_offset (schedule, 840);
    lrg_reset_schedule_set_reset_hour (schedule, 5);
    reset = unix_utc (2026, 3, 1, 15, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);
    assert_period_contract (schedule, p, reset);
}

static void
test_daily_negative_offset (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_DAILY;
    gint64 reset;

    /* 03:00 in UTC-05:00 is 08:00 UTC. */
    schedule = lrg_reset_schedule_new (3, G_DATE_TUESDAY, -300);
    reset = unix_utc (2026, 11, 2, 8, 0, 0);

    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset), ==, reset + DAY);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, reset + 1), ==, DAY - 1);

    /* UTC-12:00, reset at 23:00 local = 11:00 UTC next day. */
    lrg_reset_schedule_set_utc_offset (schedule, -720);
    lrg_reset_schedule_set_reset_hour (schedule, 23);
    reset = unix_utc (2026, 11, 3, 11, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 0), ==, 11 * 3600);
    assert_period_contract (schedule, p, reset);
    assert_period_contract (schedule, p, -1);
}

/* ========================================================================== */
/*                               Weekly periods                               */
/* ========================================================================== */

static void
test_weekly_default_tuesday (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_WEEKLY;
    gint64 tuesday;

    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 0);

    /* The first Tuesday after the Thursday epoch is 1970-01-06. */
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, 0), ==, 5 * DAY);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 5 * DAY), ==, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 5 * DAY - 1), ==, -1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, 0), ==, -1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, -1), ==, -1);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, p, -1), ==, -2 * DAY);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, -2 * DAY - 1), ==, -2);

    tuesday = unix_utc (2024, 1, 2, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, tuesday - 1), ==, tuesday);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, tuesday), ==, tuesday + WEEK);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, tuesday + 1), ==, tuesday + WEEK);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, tuesday), ==, WEEK);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, tuesday - 1), ==, 1);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, tuesday) -
                     lrg_reset_schedule_get_period (schedule, p, tuesday - 1), ==, 1);
}

static void
test_weekly_rollover_month_year_leap (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_WEEKLY;
    gint64 before;
    gint64 after;

    schedule = lrg_reset_schedule_new (0, G_DATE_TUESDAY, 0);

    /* Across the 2024 leap day: Tue 27 Feb -> Tue 5 Mar. */
    before = unix_utc (2024, 2, 27, 0, 0, 0);
    after = unix_utc (2024, 3, 5, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, before), ==, after);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, unix_utc (2024, 2, 29, 12, 0, 0)), ==,
                     lrg_reset_schedule_get_period (schedule, p, before));
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, after), ==,
                     lrg_reset_schedule_get_period (schedule, p, before) + 1);

    /* Across a month end: Tue 30 Apr 2024 -> Tue 7 May. */
    before = unix_utc (2024, 4, 30, 0, 0, 0);
    after = unix_utc (2024, 5, 7, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, before + 1), ==, after);

    /* Across a year end: Tue 31 Dec 2024 -> Tue 7 Jan 2025. */
    before = unix_utc (2024, 12, 31, 0, 0, 0);
    after = unix_utc (2025, 1, 7, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, before), ==, after);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, unix_utc (2025, 1, 1, 0, 0, 0)), ==,
                     lrg_reset_schedule_get_period (schedule, p, before));
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, after - 1), ==,
                     lrg_reset_schedule_get_period (schedule, p, before));

    /* 2000 is a leap year (divisible by 400): Tue 29 Feb 2000 is a reset. */
    before = unix_utc (2000, 2, 29, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, before) -
                     lrg_reset_schedule_get_period (schedule, p, before - 1), ==, 1);
}

static void
test_weekly_offsets_and_weekday (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    LrgResetPeriod p = LRG_RESET_PERIOD_WEEKLY;
    gint64 reset;

    /* Wednesday 07:00 in UTC+09:00 = Tuesday 22:00 UTC. */
    schedule = lrg_reset_schedule_new (7, G_DATE_WEDNESDAY, 540);
    reset = unix_utc (2026, 9, 22, 22, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset), ==, reset + WEEK);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);

    /* Monday 23:00 in UTC-08:00 = Tuesday 07:00 UTC. */
    lrg_reset_schedule_set_weekly_day (schedule, G_DATE_MONDAY);
    lrg_reset_schedule_set_reset_hour (schedule, 23);
    lrg_reset_schedule_set_utc_offset (schedule, -480);
    reset = unix_utc (2026, 9, 29, 7, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_seconds_until_reset (schedule, p, reset), ==, WEEK);

    /* Sunday is weekday 7. */
    lrg_reset_schedule_set_weekly_day (schedule, G_DATE_SUNDAY);
    lrg_reset_schedule_set_reset_hour (schedule, 0);
    lrg_reset_schedule_set_utc_offset (schedule, 0);
    reset = unix_utc (2026, 9, 27, 0, 0, 0);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, p, reset - 1), ==, reset);
    g_assert_cmpint (lrg_reset_schedule_get_period (schedule, p, reset) -
                     lrg_reset_schedule_get_period (schedule, p, reset - 1), ==, 1);
}

static void
test_is_same_period (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    gint64 reset;

    schedule = lrg_reset_schedule_new (4, G_DATE_TUESDAY, 0);
    reset = unix_utc (2026, 9, 24, 4, 0, 0);
    g_assert_true (lrg_reset_schedule_is_same_period (schedule, LRG_RESET_PERIOD_DAILY,
                                                      reset, reset + DAY - 1));
    g_assert_false (lrg_reset_schedule_is_same_period (schedule, LRG_RESET_PERIOD_DAILY,
                                                       reset - 1, reset));
    g_assert_true (lrg_reset_schedule_is_same_period (schedule, LRG_RESET_PERIOD_WEEKLY,
                                                      reset - 1, reset));
}

/* ========================================================================== */
/*                       Oracle sweep and extreme values                      */
/* ========================================================================== */

static void
test_oracle_sweep (void)
{
    static const gint offsets[] = { -720, -570, -300, 0, 60, 330, 345, 540, 840 };
    g_autoptr(LrgResetSchedule) schedule = NULL;
    g_autoptr(GRand) rand = NULL;
    guint hour;
    guint day;
    guint o;
    guint k;

    schedule = lrg_reset_schedule_new (0, 1, 0);
    rand = g_rand_new_with_seed (0x5eed);

    for (hour = 0; hour < 24; hour += 5)
    {
        for (day = 1; day <= 7; day++)
        {
            for (o = 0; o < G_N_ELEMENTS (offsets); o++)
            {
                lrg_reset_schedule_set_reset_hour (schedule, hour);
                lrg_reset_schedule_set_reset_minute (schedule, (hour * 7) % 60);
                lrg_reset_schedule_set_weekly_day (schedule, day);
                lrg_reset_schedule_set_utc_offset (schedule, offsets[o]);

                /* Fixed interesting points plus random times in years ~0100..9900. */
                assert_period_contract (schedule, LRG_RESET_PERIOD_DAILY, 0);
                assert_period_contract (schedule, LRG_RESET_PERIOD_WEEKLY, 0);
                assert_period_contract (schedule, LRG_RESET_PERIOD_DAILY, -1);
                assert_period_contract (schedule, LRG_RESET_PERIOD_WEEKLY, -1);
                for (k = 0; k < 8; k++)
                {
                    gint64 t = (gint64)g_rand_double_range (rand, -58000000000.0, 250000000000.0);
                    assert_period_contract (schedule, LRG_RESET_PERIOD_DAILY, t);
                    assert_period_contract (schedule, LRG_RESET_PERIOD_WEEKLY, t);
                }
            }
        }
    }
}

static void
test_consecutive_periods (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    gint64 index;
    gint64 t;
    gint   i;

    /* Walking reset to reset visits every index exactly once. */
    schedule = lrg_reset_schedule_new (6, G_DATE_FRIDAY, -210);
    t = -3 * WEEK;
    index = lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_DAILY, t);
    for (i = 0; i < 60; i++)
    {
        t = lrg_reset_schedule_get_next_reset (schedule, LRG_RESET_PERIOD_DAILY, t);
        g_assert_cmpint (lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_DAILY, t), ==,
                         index + 1 + i);
    }
    t = -3 * WEEK;
    index = lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_WEEKLY, t);
    for (i = 0; i < 20; i++)
    {
        t = lrg_reset_schedule_get_next_reset (schedule, LRG_RESET_PERIOD_WEEKLY, t);
        g_assert_cmpint (lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_WEEKLY, t), ==,
                         index + 1 + i);
    }
}

static void
test_extreme_values (void)
{
    g_autoptr(LrgResetSchedule) schedule = NULL;
    gint64 index;

    schedule = lrg_reset_schedule_new (23, G_DATE_SUNDAY, -720);

    /* No overflow at the ends of the gint64 range. */
    index = lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_DAILY, G_MAXINT64);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, LRG_RESET_PERIOD_DAILY, index), <=,
                     G_MAXINT64);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, LRG_RESET_PERIOD_DAILY, G_MAXINT64), ==,
                     G_MAXINT64);
    index = lrg_reset_schedule_get_period (schedule, LRG_RESET_PERIOD_WEEKLY, G_MININT64);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, LRG_RESET_PERIOD_WEEKLY, index + 1), >,
                     G_MININT64);
    g_assert_cmpint (lrg_reset_schedule_get_next_reset (schedule, LRG_RESET_PERIOD_WEEKLY, G_MININT64), >,
                     G_MININT64);

    /* Start of unrepresentable indices saturates. */
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, LRG_RESET_PERIOD_DAILY, G_MAXINT64), ==,
                     G_MAXINT64);
    g_assert_cmpint (lrg_reset_schedule_get_period_start (schedule, LRG_RESET_PERIOD_WEEKLY, G_MININT64), ==,
                     G_MININT64);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/reset-schedule/defaults", test_defaults);
    g_test_add_func ("/reset-schedule/new", test_new);
    g_test_add_func ("/reset-schedule/properties", test_properties);
    g_test_add_func ("/reset-schedule/setters-reject-out-of-range", test_setters_reject_out_of_range);
    g_test_add_func ("/reset-schedule/new-rejects-out-of-range", test_new_rejects_out_of_range);
    g_test_add_func ("/reset-schedule/daily/midnight-utc", test_daily_midnight_utc);
    g_test_add_func ("/reset-schedule/daily/negative-times", test_daily_negative_times);
    g_test_add_func ("/reset-schedule/daily/hour-and-minute", test_daily_reset_hour_and_minute);
    g_test_add_func ("/reset-schedule/daily/positive-offset", test_daily_positive_offset);
    g_test_add_func ("/reset-schedule/daily/negative-offset", test_daily_negative_offset);
    g_test_add_func ("/reset-schedule/weekly/default-tuesday", test_weekly_default_tuesday);
    g_test_add_func ("/reset-schedule/weekly/rollover-month-year-leap", test_weekly_rollover_month_year_leap);
    g_test_add_func ("/reset-schedule/weekly/offsets-and-weekday", test_weekly_offsets_and_weekday);
    g_test_add_func ("/reset-schedule/is-same-period", test_is_same_period);
    g_test_add_func ("/reset-schedule/oracle-sweep", test_oracle_sweep);
    g_test_add_func ("/reset-schedule/consecutive-periods", test_consecutive_periods);
    g_test_add_func ("/reset-schedule/extreme-values", test_extreme_values);

    return g_test_run ();
}
