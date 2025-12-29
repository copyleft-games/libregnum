/* lrg-day-night-cycle.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Day/night cycle manager implementation.
 */

#include "lrg-day-night-cycle.h"
#include "../lrg-log.h"
#include <math.h>

struct _LrgDayNightCycle
{
    GObject parent_instance;

    gfloat   current_time;    /* 0.0 to 1.0, where 0.0 = midnight */
    gfloat   day_length;      /* Length of full day in real seconds */
    gboolean paused;

    /* Time-of-day colors */
    guint8 dawn_r, dawn_g, dawn_b;
    guint8 day_r, day_g, day_b;
    guint8 dusk_r, dusk_g, dusk_b;
    guint8 night_r, night_g, night_b;

    /* Brightness values for each period */
    gfloat dawn_brightness;
    gfloat day_brightness;
    gfloat dusk_brightness;
    gfloat night_brightness;
};

G_DEFINE_FINAL_TYPE (LrgDayNightCycle, lrg_day_night_cycle, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_TIME,
    PROP_DAY_LENGTH,
    PROP_PAUSED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TIME_OF_DAY_CHANGED,
    SIGNAL_DAWN,
    SIGNAL_NOON,
    SIGNAL_DUSK,
    SIGNAL_MIDNIGHT,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * Time periods (in normalized time 0.0-1.0):
 * 0.00 - 0.20: Night (00:00 - 04:48)
 * 0.20 - 0.30: Dawn  (04:48 - 07:12)
 * 0.30 - 0.45: Morning (07:12 - 10:48)
 * 0.45 - 0.55: Noon (10:48 - 13:12)
 * 0.55 - 0.70: Afternoon (13:12 - 16:48)
 * 0.70 - 0.80: Dusk (16:48 - 19:12)
 * 0.80 - 1.00: Night (19:12 - 00:00)
 */

static LrgTimeOfDay
get_time_of_day_for_time (gfloat time)
{
    if (time < 0.20f || time >= 0.80f)
        return LRG_TIME_OF_DAY_NIGHT;
    else if (time < 0.30f)
        return LRG_TIME_OF_DAY_DAWN;
    else if (time < 0.45f)
        return LRG_TIME_OF_DAY_MORNING;
    else if (time < 0.55f)
        return LRG_TIME_OF_DAY_NOON;
    else if (time < 0.70f)
        return LRG_TIME_OF_DAY_AFTERNOON;
    else
        return LRG_TIME_OF_DAY_DUSK;
}

static guint8
lerp_color (guint8 a, guint8 b, gfloat t)
{
    return (guint8)(a + (b - a) * t);
}

static void
lrg_day_night_cycle_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgDayNightCycle *self = LRG_DAY_NIGHT_CYCLE (object);

    switch (prop_id)
    {
    case PROP_TIME:
        g_value_set_float (value, self->current_time);
        break;
    case PROP_DAY_LENGTH:
        g_value_set_float (value, self->day_length);
        break;
    case PROP_PAUSED:
        g_value_set_boolean (value, self->paused);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_day_night_cycle_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgDayNightCycle *self = LRG_DAY_NIGHT_CYCLE (object);

    switch (prop_id)
    {
    case PROP_TIME:
        lrg_day_night_cycle_set_time (self, g_value_get_float (value));
        break;
    case PROP_DAY_LENGTH:
        self->day_length = g_value_get_float (value);
        break;
    case PROP_PAUSED:
        self->paused = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_day_night_cycle_class_init (LrgDayNightCycleClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->get_property = lrg_day_night_cycle_get_property;
    object_class->set_property = lrg_day_night_cycle_set_property;

    /**
     * LrgDayNightCycle:time:
     *
     * Current time as normalized value (0.0 = midnight, 0.5 = noon).
     */
    properties[PROP_TIME] =
        g_param_spec_float ("time", "Time", "Current time (0.0-1.0)",
                            0.0f, 1.0f, 0.25f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDayNightCycle:day-length:
     *
     * Length of a full day in real seconds.
     */
    properties[PROP_DAY_LENGTH] =
        g_param_spec_float ("day-length", "Day Length", "Day length in seconds",
                            1.0f, 86400.0f, 600.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgDayNightCycle:paused:
     *
     * Whether the cycle is paused.
     */
    properties[PROP_PAUSED] =
        g_param_spec_boolean ("paused", "Paused", "Whether cycle is paused",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgDayNightCycle::time-of-day-changed:
     * @self: the #LrgDayNightCycle
     * @time_of_day: the new #LrgTimeOfDay
     *
     * Emitted when the time-of-day period changes.
     */
    signals[SIGNAL_TIME_OF_DAY_CHANGED] =
        g_signal_new ("time-of-day-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_INT);

    /**
     * LrgDayNightCycle::dawn:
     * @self: the #LrgDayNightCycle
     *
     * Emitted when dawn begins.
     */
    signals[SIGNAL_DAWN] =
        g_signal_new ("dawn",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgDayNightCycle::noon:
     * @self: the #LrgDayNightCycle
     *
     * Emitted when noon is reached.
     */
    signals[SIGNAL_NOON] =
        g_signal_new ("noon",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgDayNightCycle::dusk:
     * @self: the #LrgDayNightCycle
     *
     * Emitted when dusk begins.
     */
    signals[SIGNAL_DUSK] =
        g_signal_new ("dusk",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgDayNightCycle::midnight:
     * @self: the #LrgDayNightCycle
     *
     * Emitted when midnight is reached.
     */
    signals[SIGNAL_MIDNIGHT] =
        g_signal_new ("midnight",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_day_night_cycle_init (LrgDayNightCycle *self)
{
    self->current_time = 0.25f;  /* Start at 6:00 AM */
    self->day_length = 600.0f;   /* 10 minute day */
    self->paused = FALSE;

    /* Default colors */
    self->dawn_r = 255; self->dawn_g = 180; self->dawn_b = 100;   /* Orange-ish */
    self->day_r = 255; self->day_g = 255; self->day_b = 255;       /* White */
    self->dusk_r = 255; self->dusk_g = 140; self->dusk_b = 80;    /* Deep orange */
    self->night_r = 40; self->night_g = 40; self->night_b = 80;   /* Dark blue */

    /* Default brightness */
    self->dawn_brightness = 0.6f;
    self->day_brightness = 1.0f;
    self->dusk_brightness = 0.5f;
    self->night_brightness = 0.2f;
}

/* Public API */

LrgDayNightCycle *
lrg_day_night_cycle_new (void)
{
    return g_object_new (LRG_TYPE_DAY_NIGHT_CYCLE, NULL);
}

gfloat
lrg_day_night_cycle_get_time (LrgDayNightCycle *self)
{
    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), 0.0f);
    return self->current_time;
}

void
lrg_day_night_cycle_set_time (LrgDayNightCycle *self,
                              gfloat            time)
{
    LrgTimeOfDay old_tod, new_tod;

    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));

    old_tod = get_time_of_day_for_time (self->current_time);

    /* Wrap time to 0.0-1.0 range */
    time = fmodf (time, 1.0f);
    if (time < 0.0f)
        time += 1.0f;

    self->current_time = time;
    new_tod = get_time_of_day_for_time (time);

    if (old_tod != new_tod)
    {
        g_signal_emit (self, signals[SIGNAL_TIME_OF_DAY_CHANGED], 0, (gint)new_tod);

        /* Emit specific signals */
        if (new_tod == LRG_TIME_OF_DAY_DAWN)
            g_signal_emit (self, signals[SIGNAL_DAWN], 0);
        else if (new_tod == LRG_TIME_OF_DAY_NOON)
            g_signal_emit (self, signals[SIGNAL_NOON], 0);
        else if (new_tod == LRG_TIME_OF_DAY_DUSK)
            g_signal_emit (self, signals[SIGNAL_DUSK], 0);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIME]);
}

LrgTimeOfDay
lrg_day_night_cycle_get_time_of_day (LrgDayNightCycle *self)
{
    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), LRG_TIME_OF_DAY_NOON);
    return get_time_of_day_for_time (self->current_time);
}

gfloat
lrg_day_night_cycle_get_hours (LrgDayNightCycle *self)
{
    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), 0.0f);
    return self->current_time * 24.0f;
}

void
lrg_day_night_cycle_set_hours (LrgDayNightCycle *self,
                               gfloat            hours)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    lrg_day_night_cycle_set_time (self, hours / 24.0f);
}

gfloat
lrg_day_night_cycle_get_day_length (LrgDayNightCycle *self)
{
    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), 0.0f);
    return self->day_length;
}

void
lrg_day_night_cycle_set_day_length (LrgDayNightCycle *self,
                                    gfloat            seconds)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    g_return_if_fail (seconds > 0.0f);
    self->day_length = seconds;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DAY_LENGTH]);
}

gboolean
lrg_day_night_cycle_get_paused (LrgDayNightCycle *self)
{
    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), FALSE);
    return self->paused;
}

void
lrg_day_night_cycle_set_paused (LrgDayNightCycle *self,
                                gboolean          paused)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    self->paused = paused;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAUSED]);
}

void
lrg_day_night_cycle_get_ambient_color (LrgDayNightCycle *self,
                                       guint8           *r,
                                       guint8           *g,
                                       guint8           *b)
{
    gfloat t;
    guint8 out_r, out_g, out_b;

    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));

    /*
     * Blend colors based on time:
     * Night (0.0-0.20) -> Dawn (0.20-0.30) -> Day (0.30-0.70) -> Dusk (0.70-0.80) -> Night (0.80-1.0)
     */

    if (self->current_time < 0.20f)
    {
        /* Night */
        out_r = self->night_r;
        out_g = self->night_g;
        out_b = self->night_b;
    }
    else if (self->current_time < 0.25f)
    {
        /* Night to dawn transition */
        t = (self->current_time - 0.20f) / 0.05f;
        out_r = lerp_color (self->night_r, self->dawn_r, t);
        out_g = lerp_color (self->night_g, self->dawn_g, t);
        out_b = lerp_color (self->night_b, self->dawn_b, t);
    }
    else if (self->current_time < 0.30f)
    {
        /* Dawn to day transition */
        t = (self->current_time - 0.25f) / 0.05f;
        out_r = lerp_color (self->dawn_r, self->day_r, t);
        out_g = lerp_color (self->dawn_g, self->day_g, t);
        out_b = lerp_color (self->dawn_b, self->day_b, t);
    }
    else if (self->current_time < 0.70f)
    {
        /* Day */
        out_r = self->day_r;
        out_g = self->day_g;
        out_b = self->day_b;
    }
    else if (self->current_time < 0.75f)
    {
        /* Day to dusk transition */
        t = (self->current_time - 0.70f) / 0.05f;
        out_r = lerp_color (self->day_r, self->dusk_r, t);
        out_g = lerp_color (self->day_g, self->dusk_g, t);
        out_b = lerp_color (self->day_b, self->dusk_b, t);
    }
    else if (self->current_time < 0.80f)
    {
        /* Dusk to night transition */
        t = (self->current_time - 0.75f) / 0.05f;
        out_r = lerp_color (self->dusk_r, self->night_r, t);
        out_g = lerp_color (self->dusk_g, self->night_g, t);
        out_b = lerp_color (self->dusk_b, self->night_b, t);
    }
    else
    {
        /* Night */
        out_r = self->night_r;
        out_g = self->night_g;
        out_b = self->night_b;
    }

    if (r) *r = out_r;
    if (g) *g = out_g;
    if (b) *b = out_b;
}

gfloat
lrg_day_night_cycle_get_ambient_brightness (LrgDayNightCycle *self)
{
    gfloat t;

    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), 1.0f);

    /* Blend brightness similar to colors */
    if (self->current_time < 0.20f)
        return self->night_brightness;
    else if (self->current_time < 0.25f)
    {
        t = (self->current_time - 0.20f) / 0.05f;
        return self->night_brightness + (self->dawn_brightness - self->night_brightness) * t;
    }
    else if (self->current_time < 0.30f)
    {
        t = (self->current_time - 0.25f) / 0.05f;
        return self->dawn_brightness + (self->day_brightness - self->dawn_brightness) * t;
    }
    else if (self->current_time < 0.70f)
        return self->day_brightness;
    else if (self->current_time < 0.75f)
    {
        t = (self->current_time - 0.70f) / 0.05f;
        return self->day_brightness + (self->dusk_brightness - self->day_brightness) * t;
    }
    else if (self->current_time < 0.80f)
    {
        t = (self->current_time - 0.75f) / 0.05f;
        return self->dusk_brightness + (self->night_brightness - self->dusk_brightness) * t;
    }
    else
        return self->night_brightness;
}

gfloat
lrg_day_night_cycle_get_sun_angle (LrgDayNightCycle *self)
{
    gfloat sun_time;

    g_return_val_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self), 0.0f);

    /*
     * Sun rises at 0.25 (6 AM), sets at 0.75 (6 PM)
     * Angle: 0 = horizon (east), 90 = overhead, 180 = horizon (west)
     */
    sun_time = self->current_time;

    if (sun_time < 0.25f || sun_time > 0.75f)
        return -1.0f;  /* Sun not visible */

    return (sun_time - 0.25f) / 0.5f * 180.0f;
}

void
lrg_day_night_cycle_set_dawn_color (LrgDayNightCycle *self,
                                    guint8            r,
                                    guint8            g,
                                    guint8            b)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    self->dawn_r = r;
    self->dawn_g = g;
    self->dawn_b = b;
}

void
lrg_day_night_cycle_set_day_color (LrgDayNightCycle *self,
                                   guint8            r,
                                   guint8            g,
                                   guint8            b)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    self->day_r = r;
    self->day_g = g;
    self->day_b = b;
}

void
lrg_day_night_cycle_set_dusk_color (LrgDayNightCycle *self,
                                    guint8            r,
                                    guint8            g,
                                    guint8            b)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    self->dusk_r = r;
    self->dusk_g = g;
    self->dusk_b = b;
}

void
lrg_day_night_cycle_set_night_color (LrgDayNightCycle *self,
                                     guint8            r,
                                     guint8            g,
                                     guint8            b)
{
    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));
    self->night_r = r;
    self->night_g = g;
    self->night_b = b;
}

void
lrg_day_night_cycle_update (LrgDayNightCycle *self,
                            gfloat            delta_time)
{
    gfloat old_time;
    gfloat new_time;

    g_return_if_fail (LRG_IS_DAY_NIGHT_CYCLE (self));

    if (self->paused || self->day_length <= 0.0f)
        return;

    old_time = self->current_time;
    new_time = old_time + delta_time / self->day_length;

    /* Check for midnight crossing */
    if (new_time >= 1.0f && old_time < 1.0f)
        g_signal_emit (self, signals[SIGNAL_MIDNIGHT], 0);

    lrg_day_night_cycle_set_time (self, new_time);
}
