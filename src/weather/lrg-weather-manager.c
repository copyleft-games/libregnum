/* lrg-weather-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Weather system manager implementation.
 */

#include "lrg-weather-manager.h"
#include "../lrg-log.h"

struct _LrgWeatherManager
{
    GObject parent_instance;

    GHashTable       *registered_weather;  /* id -> LrgWeather */
    LrgWeather       *active_weather;
    LrgWeather       *previous_weather;    /* For transitions */

    gboolean          transitioning;
    gfloat            transition_duration;
    gfloat            transition_progress;

    gfloat            wind_x;
    gfloat            wind_y;

    LrgDayNightCycle *day_night_cycle;
    gboolean          day_night_enabled;
};

G_DEFINE_FINAL_TYPE (LrgWeatherManager, lrg_weather_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_WIND_X,
    PROP_WIND_Y,
    PROP_DAY_NIGHT_ENABLED,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_WEATHER_CHANGED,
    SIGNAL_TRANSITION_STARTED,
    SIGNAL_TRANSITION_COMPLETED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_weather_manager_dispose (GObject *object)
{
    LrgWeatherManager *self = LRG_WEATHER_MANAGER (object);

    /* Deactivate any active weather */
    if (self->active_weather != NULL)
    {
        lrg_weather_deactivate (self->active_weather);
        self->active_weather = NULL;
    }

    if (self->previous_weather != NULL)
    {
        lrg_weather_deactivate (self->previous_weather);
        self->previous_weather = NULL;
    }

    g_clear_pointer (&self->registered_weather, g_hash_table_destroy);
    g_clear_object (&self->day_night_cycle);

    G_OBJECT_CLASS (lrg_weather_manager_parent_class)->dispose (object);
}

static void
lrg_weather_manager_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgWeatherManager *self = LRG_WEATHER_MANAGER (object);

    switch (prop_id)
    {
    case PROP_WIND_X:
        g_value_set_float (value, self->wind_x);
        break;
    case PROP_WIND_Y:
        g_value_set_float (value, self->wind_y);
        break;
    case PROP_DAY_NIGHT_ENABLED:
        g_value_set_boolean (value, self->day_night_enabled);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_manager_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgWeatherManager *self = LRG_WEATHER_MANAGER (object);

    switch (prop_id)
    {
    case PROP_WIND_X:
        lrg_weather_manager_set_wind (self, g_value_get_float (value), self->wind_y);
        break;
    case PROP_WIND_Y:
        lrg_weather_manager_set_wind (self, self->wind_x, g_value_get_float (value));
        break;
    case PROP_DAY_NIGHT_ENABLED:
        self->day_night_enabled = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_weather_manager_class_init (LrgWeatherManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = lrg_weather_manager_dispose;
    object_class->get_property = lrg_weather_manager_get_property;
    object_class->set_property = lrg_weather_manager_set_property;

    /**
     * LrgWeatherManager:wind-x:
     *
     * Global wind X component.
     */
    properties[PROP_WIND_X] =
        g_param_spec_float ("wind-x", "Wind X", "Wind X component",
                            -1000.0f, 1000.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherManager:wind-y:
     *
     * Global wind Y component.
     */
    properties[PROP_WIND_Y] =
        g_param_spec_float ("wind-y", "Wind Y", "Wind Y component",
                            -1000.0f, 1000.0f, 0.0f,
                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgWeatherManager:day-night-enabled:
     *
     * Whether day/night cycle affects ambient lighting.
     */
    properties[PROP_DAY_NIGHT_ENABLED] =
        g_param_spec_boolean ("day-night-enabled", "Day/Night Enabled",
                              "Whether day/night cycle is enabled",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgWeatherManager::weather-changed:
     * @self: the #LrgWeatherManager
     * @previous_id: (nullable): previous weather ID
     * @new_id: (nullable): new weather ID
     *
     * Emitted when the active weather changes.
     */
    signals[SIGNAL_WEATHER_CHANGED] =
        g_signal_new ("weather-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

    /**
     * LrgWeatherManager::transition-started:
     * @self: the #LrgWeatherManager
     * @from_id: (nullable): source weather ID
     * @to_id: (nullable): target weather ID
     *
     * Emitted when a weather transition starts.
     */
    signals[SIGNAL_TRANSITION_STARTED] =
        g_signal_new ("transition-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);

    /**
     * LrgWeatherManager::transition-completed:
     * @self: the #LrgWeatherManager
     *
     * Emitted when a weather transition completes.
     */
    signals[SIGNAL_TRANSITION_COMPLETED] =
        g_signal_new ("transition-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_weather_manager_init (LrgWeatherManager *self)
{
    self->registered_weather = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                      g_free, g_object_unref);
    self->active_weather = NULL;
    self->previous_weather = NULL;
    self->transitioning = FALSE;
    self->transition_duration = 0.0f;
    self->transition_progress = 0.0f;
    self->wind_x = 0.0f;
    self->wind_y = 0.0f;
    self->day_night_cycle = lrg_day_night_cycle_new ();
    self->day_night_enabled = TRUE;
}

/* Public API */

LrgWeatherManager *
lrg_weather_manager_new (void)
{
    return g_object_new (LRG_TYPE_WEATHER_MANAGER, NULL);
}

void
lrg_weather_manager_register_weather (LrgWeatherManager *self,
                                      LrgWeather        *weather)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));
    g_return_if_fail (LRG_IS_WEATHER (weather));

    id = lrg_weather_get_id (weather);
    g_return_if_fail (id != NULL);

    g_hash_table_insert (self->registered_weather,
                         g_strdup (id),
                         g_object_ref (weather));

    lrg_debug (LRG_LOG_DOMAIN_WEATHER, "Registered weather: %s", id);
}

gboolean
lrg_weather_manager_unregister_weather (LrgWeatherManager *self,
                                        const gchar       *weather_id)
{
    LrgWeather *weather;

    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), FALSE);
    g_return_val_if_fail (weather_id != NULL, FALSE);

    weather = g_hash_table_lookup (self->registered_weather, weather_id);
    if (weather == NULL)
        return FALSE;

    /* Don't allow unregistering active weather */
    if (weather == self->active_weather || weather == self->previous_weather)
    {
        lrg_warning (LRG_LOG_DOMAIN_WEATHER,
                     "Cannot unregister active weather: %s", weather_id);
        return FALSE;
    }

    return g_hash_table_remove (self->registered_weather, weather_id);
}

LrgWeather *
lrg_weather_manager_get_weather (LrgWeatherManager *self,
                                 const gchar       *weather_id)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), NULL);
    g_return_val_if_fail (weather_id != NULL, NULL);

    return g_hash_table_lookup (self->registered_weather, weather_id);
}

GList *
lrg_weather_manager_get_registered_weather (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), NULL);
    return g_hash_table_get_values (self->registered_weather);
}

void
lrg_weather_manager_set_weather (LrgWeatherManager *self,
                                 const gchar       *weather_id,
                                 gfloat             transition_duration)
{
    LrgWeather *new_weather = NULL;
    const gchar *prev_id = NULL;

    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    /* Look up new weather if ID provided */
    if (weather_id != NULL)
    {
        new_weather = g_hash_table_lookup (self->registered_weather, weather_id);
        if (new_weather == NULL)
        {
            lrg_warning (LRG_LOG_DOMAIN_WEATHER,
                         "Weather not found: %s", weather_id);
            return;
        }
    }

    /* Check if already active */
    if (new_weather == self->active_weather)
        return;

    prev_id = self->active_weather ? lrg_weather_get_id (self->active_weather) : NULL;

    /* Handle instant transition */
    if (transition_duration <= 0.0f)
    {
        /* Deactivate previous */
        if (self->active_weather != NULL)
            lrg_weather_deactivate (self->active_weather);

        /* Activate new */
        self->active_weather = new_weather;
        if (new_weather != NULL)
        {
            lrg_weather_set_wind (new_weather, self->wind_x, self->wind_y);
            lrg_weather_activate (new_weather);
        }

        g_signal_emit (self, signals[SIGNAL_WEATHER_CHANGED], 0, prev_id, weather_id);
        return;
    }

    /* Start gradual transition */
    if (self->previous_weather != NULL)
        lrg_weather_deactivate (self->previous_weather);

    self->previous_weather = self->active_weather;
    self->active_weather = new_weather;
    self->transitioning = TRUE;
    self->transition_duration = transition_duration;
    self->transition_progress = 0.0f;

    /* Activate new weather with zero intensity */
    if (new_weather != NULL)
    {
        GList *effects = lrg_weather_get_effects (new_weather);
        GList *l;

        for (l = effects; l != NULL; l = l->next)
        {
            LrgWeatherEffect *effect = l->data;
            lrg_weather_effect_set_intensity (effect, 0.0f);
        }

        g_list_free (effects);
        lrg_weather_set_wind (new_weather, self->wind_x, self->wind_y);
        lrg_weather_activate (new_weather);
    }

    g_signal_emit (self, signals[SIGNAL_TRANSITION_STARTED], 0, prev_id, weather_id);

    lrg_debug (LRG_LOG_DOMAIN_WEATHER,
               "Weather transition started: %s -> %s (%.1fs)",
               prev_id ? prev_id : "(clear)",
               weather_id ? weather_id : "(clear)",
               transition_duration);
}

LrgWeather *
lrg_weather_manager_get_active_weather (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), NULL);
    return self->active_weather;
}

const gchar *
lrg_weather_manager_get_active_weather_id (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), NULL);

    if (self->active_weather == NULL)
        return NULL;

    return lrg_weather_get_id (self->active_weather);
}

gboolean
lrg_weather_manager_is_transitioning (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), FALSE);
    return self->transitioning;
}

void
lrg_weather_manager_clear_weather (LrgWeatherManager *self,
                                   gfloat             transition_duration)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));
    lrg_weather_manager_set_weather (self, NULL, transition_duration);
}

void
lrg_weather_manager_get_wind (LrgWeatherManager *self,
                              gfloat            *wind_x,
                              gfloat            *wind_y)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    if (wind_x) *wind_x = self->wind_x;
    if (wind_y) *wind_y = self->wind_y;
}

void
lrg_weather_manager_set_wind (LrgWeatherManager *self,
                              gfloat             wind_x,
                              gfloat             wind_y)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    self->wind_x = wind_x;
    self->wind_y = wind_y;

    /* Propagate to active weather */
    if (self->active_weather != NULL)
        lrg_weather_set_wind (self->active_weather, wind_x, wind_y);

    if (self->previous_weather != NULL)
        lrg_weather_set_wind (self->previous_weather, wind_x, wind_y);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_X]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_WIND_Y]);
}

LrgDayNightCycle *
lrg_weather_manager_get_day_night_cycle (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), NULL);
    return self->day_night_cycle;
}

gboolean
lrg_weather_manager_get_day_night_enabled (LrgWeatherManager *self)
{
    g_return_val_if_fail (LRG_IS_WEATHER_MANAGER (self), FALSE);
    return self->day_night_enabled;
}

void
lrg_weather_manager_set_day_night_enabled (LrgWeatherManager *self,
                                           gboolean           enabled)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));
    self->day_night_enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DAY_NIGHT_ENABLED]);
}

void
lrg_weather_manager_get_combined_ambient (LrgWeatherManager *self,
                                          guint8            *r,
                                          guint8            *g,
                                          guint8            *b,
                                          gfloat            *brightness)
{
    guint8 dn_r = 255, dn_g = 255, dn_b = 255;
    gfloat dn_brightness = 1.0f;
    guint8 w_r = 255, w_g = 255, w_b = 255;
    gfloat w_brightness = 1.0f;

    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    /* Get day/night colors */
    if (self->day_night_enabled)
    {
        lrg_day_night_cycle_get_ambient_color (self->day_night_cycle,
                                               &dn_r, &dn_g, &dn_b);
        dn_brightness = lrg_day_night_cycle_get_ambient_brightness (self->day_night_cycle);
    }

    /* Get weather colors */
    if (self->active_weather != NULL)
    {
        lrg_weather_get_ambient_color (self->active_weather, &w_r, &w_g, &w_b);
        w_brightness = lrg_weather_get_ambient_brightness (self->active_weather);
    }

    /* Combine: multiply colors and brightness */
    if (r) *r = (guint8)((dn_r * w_r) / 255);
    if (g) *g = (guint8)((dn_g * w_g) / 255);
    if (b) *b = (guint8)((dn_b * w_b) / 255);
    if (brightness) *brightness = dn_brightness * w_brightness;
}

void
lrg_weather_manager_update (LrgWeatherManager *self,
                            gfloat             delta_time)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    /* Update day/night cycle */
    if (self->day_night_enabled)
        lrg_day_night_cycle_update (self->day_night_cycle, delta_time);

    /* Handle weather transition */
    if (self->transitioning)
    {
        gfloat t;

        self->transition_progress += delta_time;
        t = self->transition_progress / self->transition_duration;

        if (t >= 1.0f)
        {
            /* Transition complete */
            t = 1.0f;
            self->transitioning = FALSE;

            /* Deactivate previous weather */
            if (self->previous_weather != NULL)
            {
                lrg_weather_deactivate (self->previous_weather);
                self->previous_weather = NULL;
            }

            g_signal_emit (self, signals[SIGNAL_TRANSITION_COMPLETED], 0);
            g_signal_emit (self, signals[SIGNAL_WEATHER_CHANGED], 0,
                           NULL, /* prev is gone now */
                           self->active_weather ? lrg_weather_get_id (self->active_weather) : NULL);
        }

        /* Update effect intensities for transition */
        if (self->previous_weather != NULL)
        {
            GList *effects = lrg_weather_get_effects (self->previous_weather);
            GList *l;

            for (l = effects; l != NULL; l = l->next)
            {
                LrgWeatherEffect *effect = l->data;
                lrg_weather_effect_set_intensity (effect, 1.0f - t);
            }

            g_list_free (effects);
        }

        if (self->active_weather != NULL)
        {
            GList *effects = lrg_weather_get_effects (self->active_weather);
            GList *l;

            for (l = effects; l != NULL; l = l->next)
            {
                LrgWeatherEffect *effect = l->data;
                lrg_weather_effect_set_intensity (effect, t);
            }

            g_list_free (effects);
        }
    }

    /* Update active weather effects */
    if (self->previous_weather != NULL && self->transitioning)
        lrg_weather_update (self->previous_weather, delta_time);

    if (self->active_weather != NULL)
        lrg_weather_update (self->active_weather, delta_time);
}

void
lrg_weather_manager_render (LrgWeatherManager *self)
{
    g_return_if_fail (LRG_IS_WEATHER_MANAGER (self));

    /* Render previous weather (during transition) */
    if (self->previous_weather != NULL && self->transitioning)
        lrg_weather_render (self->previous_weather);

    /* Render active weather */
    if (self->active_weather != NULL)
        lrg_weather_render (self->active_weather);
}
