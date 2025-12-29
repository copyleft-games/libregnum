/* test-weather.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Unit tests for the weather module.
 */

#include "../src/lrg-enums.h"
#include "../src/weather/lrg-weather-effect.h"
#include "../src/weather/lrg-rain.h"
#include "../src/weather/lrg-snow.h"
#include "../src/weather/lrg-fog.h"
#include "../src/weather/lrg-lightning.h"
#include "../src/weather/lrg-weather.h"
#include "../src/weather/lrg-day-night-cycle.h"
#include "../src/weather/lrg-weather-manager.h"

#include <glib.h>
#include <math.h>

/* ========================================================================== */
/*                            Rain Effect Tests                                */
/* ========================================================================== */

static void
test_rain_new (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    g_assert_nonnull (rain);
    g_assert_true (LRG_IS_RAIN (rain));
    g_assert_true (LRG_IS_WEATHER_EFFECT (rain));
}

static void
test_rain_properties (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    /* Test default values */
    g_assert_cmpuint (lrg_rain_get_drop_count (rain), ==, 1000);
    g_assert_cmpfloat (lrg_rain_get_drop_speed (rain), >, 0.0f);
    g_assert_cmpfloat (lrg_rain_get_drop_length (rain), >, 0.0f);

    /* Test setters */
    lrg_rain_set_drop_count (rain, 500);
    g_assert_cmpuint (lrg_rain_get_drop_count (rain), ==, 500);

    lrg_rain_set_drop_speed (rain, 200.0f);
    g_assert_cmpfloat (lrg_rain_get_drop_speed (rain), ==, 200.0f);

    lrg_rain_set_drop_length (rain, 30.0f);
    g_assert_cmpfloat (lrg_rain_get_drop_length (rain), ==, 30.0f);
}

static void
test_rain_splash (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    /* Test splash settings */
    g_assert_true (lrg_rain_get_splash_enabled (rain));

    lrg_rain_set_splash_enabled (rain, FALSE);
    g_assert_false (lrg_rain_get_splash_enabled (rain));
}

static void
test_rain_color (void)
{
    g_autoptr(LrgRain) rain = NULL;
    guint8 r, g, b, a;

    rain = lrg_rain_new ();

    lrg_rain_set_color (rain, 100, 150, 200, 180);
    lrg_rain_get_color (rain, &r, &g, &b, &a);

    g_assert_cmpuint (r, ==, 100);
    g_assert_cmpuint (g, ==, 150);
    g_assert_cmpuint (b, ==, 200);
    g_assert_cmpuint (a, ==, 180);
}

/* ========================================================================== */
/*                            Snow Effect Tests                                */
/* ========================================================================== */

static void
test_snow_new (void)
{
    g_autoptr(LrgSnow) snow = NULL;

    snow = lrg_snow_new ();

    g_assert_nonnull (snow);
    g_assert_true (LRG_IS_SNOW (snow));
    g_assert_true (LRG_IS_WEATHER_EFFECT (snow));
}

static void
test_snow_properties (void)
{
    g_autoptr(LrgSnow) snow = NULL;

    snow = lrg_snow_new ();

    /* Test default values */
    g_assert_cmpuint (lrg_snow_get_flake_count (snow), ==, 500);
    g_assert_cmpfloat (lrg_snow_get_flake_speed (snow), >, 0.0f);
    g_assert_cmpfloat (lrg_snow_get_flake_size (snow), >, 0.0f);

    /* Test setters */
    lrg_snow_set_flake_count (snow, 1000);
    g_assert_cmpuint (lrg_snow_get_flake_count (snow), ==, 1000);

    lrg_snow_set_flake_speed (snow, 50.0f);
    g_assert_cmpfloat (lrg_snow_get_flake_speed (snow), ==, 50.0f);

    lrg_snow_set_flake_size (snow, 6.0f);
    g_assert_cmpfloat (lrg_snow_get_flake_size (snow), ==, 6.0f);
}

static void
test_snow_sway (void)
{
    g_autoptr(LrgSnow) snow = NULL;

    snow = lrg_snow_new ();

    lrg_snow_set_sway_amount (snow, 50.0f);
    g_assert_cmpfloat (lrg_snow_get_sway_amount (snow), ==, 50.0f);

    lrg_snow_set_sway_speed (snow, 3.0f);
    g_assert_cmpfloat (lrg_snow_get_sway_speed (snow), ==, 3.0f);
}

static void
test_snow_accumulation (void)
{
    g_autoptr(LrgSnow) snow = NULL;

    snow = lrg_snow_new ();

    g_assert_false (lrg_snow_get_accumulation_enabled (snow));

    lrg_snow_set_accumulation_enabled (snow, TRUE);
    g_assert_true (lrg_snow_get_accumulation_enabled (snow));

    /* Initial accumulation should be 0 */
    g_assert_cmpfloat (lrg_snow_get_accumulation_height (snow), ==, 0.0f);
}

/* ========================================================================== */
/*                             Fog Effect Tests                                */
/* ========================================================================== */

static void
test_fog_new (void)
{
    g_autoptr(LrgFog) fog = NULL;

    fog = lrg_fog_new ();

    g_assert_nonnull (fog);
    g_assert_true (LRG_IS_FOG (fog));
    g_assert_true (LRG_IS_WEATHER_EFFECT (fog));
}

static void
test_fog_type (void)
{
    g_autoptr(LrgFog) fog = NULL;

    fog = lrg_fog_new ();

    /* Test default type */
    g_assert_cmpint (lrg_fog_get_fog_type (fog), ==, LRG_FOG_TYPE_UNIFORM);

    /* Test all fog types */
    lrg_fog_set_fog_type (fog, LRG_FOG_TYPE_LINEAR);
    g_assert_cmpint (lrg_fog_get_fog_type (fog), ==, LRG_FOG_TYPE_LINEAR);

    lrg_fog_set_fog_type (fog, LRG_FOG_TYPE_EXPONENTIAL);
    g_assert_cmpint (lrg_fog_get_fog_type (fog), ==, LRG_FOG_TYPE_EXPONENTIAL);

    lrg_fog_set_fog_type (fog, LRG_FOG_TYPE_HEIGHT);
    g_assert_cmpint (lrg_fog_get_fog_type (fog), ==, LRG_FOG_TYPE_HEIGHT);
}

static void
test_fog_density (void)
{
    g_autoptr(LrgFog) fog = NULL;

    fog = lrg_fog_new ();

    g_assert_cmpfloat (lrg_fog_get_density (fog), >, 0.0f);
    g_assert_cmpfloat (lrg_fog_get_density (fog), <=, 1.0f);

    lrg_fog_set_density (fog, 0.5f);
    g_assert_cmpfloat (lrg_fog_get_density (fog), ==, 0.5f);

    /* Test clamping */
    lrg_fog_set_density (fog, 2.0f);
    g_assert_cmpfloat (lrg_fog_get_density (fog), ==, 1.0f);

    lrg_fog_set_density (fog, -1.0f);
    g_assert_cmpfloat (lrg_fog_get_density (fog), ==, 0.0f);
}

static void
test_fog_distance (void)
{
    g_autoptr(LrgFog) fog = NULL;

    fog = lrg_fog_new ();

    lrg_fog_set_start_distance (fog, 50.0f);
    g_assert_cmpfloat (lrg_fog_get_start_distance (fog), ==, 50.0f);

    lrg_fog_set_end_distance (fog, 500.0f);
    g_assert_cmpfloat (lrg_fog_get_end_distance (fog), ==, 500.0f);
}

static void
test_fog_animation (void)
{
    g_autoptr(LrgFog) fog = NULL;

    fog = lrg_fog_new ();

    g_assert_true (lrg_fog_get_animated (fog));

    lrg_fog_set_animated (fog, FALSE);
    g_assert_false (lrg_fog_get_animated (fog));

    lrg_fog_set_scroll_speed_x (fog, 20.0f);
    g_assert_cmpfloat (lrg_fog_get_scroll_speed_x (fog), ==, 20.0f);
}

/* ========================================================================== */
/*                         Lightning Effect Tests                              */
/* ========================================================================== */

static void
test_lightning_new (void)
{
    g_autoptr(LrgLightning) lightning = NULL;

    lightning = lrg_lightning_new ();

    g_assert_nonnull (lightning);
    g_assert_true (LRG_IS_LIGHTNING (lightning));
    g_assert_true (LRG_IS_WEATHER_EFFECT (lightning));
}

static void
test_lightning_interval (void)
{
    g_autoptr(LrgLightning) lightning = NULL;

    lightning = lrg_lightning_new ();

    lrg_lightning_set_min_interval (lightning, 3.0f);
    g_assert_cmpfloat (lrg_lightning_get_min_interval (lightning), ==, 3.0f);

    lrg_lightning_set_max_interval (lightning, 20.0f);
    g_assert_cmpfloat (lrg_lightning_get_max_interval (lightning), ==, 20.0f);
}

static void
test_lightning_flash (void)
{
    g_autoptr(LrgLightning) lightning = NULL;

    lightning = lrg_lightning_new ();

    lrg_lightning_set_flash_duration (lightning, 0.2f);
    g_assert_cmpfloat (lrg_lightning_get_flash_duration (lightning), ==, 0.2f);

    lrg_lightning_set_flash_count (lightning, 3);
    g_assert_cmpuint (lrg_lightning_get_flash_count (lightning), ==, 3);

    lrg_lightning_set_flash_intensity (lightning, 0.9f);
    g_assert_cmpfloat (lrg_lightning_get_flash_intensity (lightning), ==, 0.9f);
}

static void
test_lightning_thunder (void)
{
    g_autoptr(LrgLightning) lightning = NULL;

    lightning = lrg_lightning_new ();

    g_assert_true (lrg_lightning_get_thunder_enabled (lightning));

    lrg_lightning_set_thunder_enabled (lightning, FALSE);
    g_assert_false (lrg_lightning_get_thunder_enabled (lightning));

    lrg_lightning_set_thunder_delay (lightning, 2.0f);
    g_assert_cmpfloat (lrg_lightning_get_thunder_delay (lightning), ==, 2.0f);
}

static void
test_lightning_trigger (void)
{
    g_autoptr(LrgLightning) lightning = NULL;

    lightning = lrg_lightning_new ();

    g_assert_false (lrg_lightning_is_flashing (lightning));

    /* Note: trigger_flash only works when effect is active */
}

/* ========================================================================== */
/*                            Weather State Tests                              */
/* ========================================================================== */

static void
test_weather_new (void)
{
    g_autoptr(LrgWeather) weather = NULL;

    weather = lrg_weather_new ("rainy", "Rainy Weather");

    g_assert_nonnull (weather);
    g_assert_true (LRG_IS_WEATHER (weather));
    g_assert_cmpstr (lrg_weather_get_id (weather), ==, "rainy");
    g_assert_cmpstr (lrg_weather_get_name (weather), ==, "Rainy Weather");
}

static void
test_weather_active (void)
{
    g_autoptr(LrgWeather) weather = NULL;

    weather = lrg_weather_new ("clear", "Clear");

    g_assert_false (lrg_weather_is_active (weather));

    lrg_weather_activate (weather);
    g_assert_true (lrg_weather_is_active (weather));

    lrg_weather_deactivate (weather);
    g_assert_false (lrg_weather_is_active (weather));
}

static void
test_weather_effects (void)
{
    g_autoptr(LrgWeather) weather = NULL;
    g_autoptr(LrgRain) rain = NULL;
    g_autoptr(LrgFog) fog = NULL;
    GList *effects;

    weather = lrg_weather_new ("stormy", "Storm");
    rain = lrg_rain_new ();
    fog = lrg_fog_new ();

    g_assert_cmpuint (lrg_weather_get_effect_count (weather), ==, 0);

    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (rain));
    g_assert_cmpuint (lrg_weather_get_effect_count (weather), ==, 1);

    lrg_weather_add_effect (weather, LRG_WEATHER_EFFECT (fog));
    g_assert_cmpuint (lrg_weather_get_effect_count (weather), ==, 2);

    effects = lrg_weather_get_effects (weather);
    g_assert_cmpuint (g_list_length (effects), ==, 2);
    g_list_free (effects);

    g_assert_true (lrg_weather_remove_effect (weather, LRG_WEATHER_EFFECT (rain)));
    g_assert_cmpuint (lrg_weather_get_effect_count (weather), ==, 1);
}

static void
test_weather_ambient (void)
{
    g_autoptr(LrgWeather) weather = NULL;
    guint8 r, g, b;

    weather = lrg_weather_new ("foggy", "Foggy");

    lrg_weather_set_ambient_color (weather, 180, 180, 200);
    lrg_weather_get_ambient_color (weather, &r, &g, &b);

    g_assert_cmpuint (r, ==, 180);
    g_assert_cmpuint (g, ==, 180);
    g_assert_cmpuint (b, ==, 200);

    lrg_weather_set_ambient_brightness (weather, 0.7f);
    g_assert_cmpfloat (lrg_weather_get_ambient_brightness (weather), ==, 0.7f);
}

static void
test_weather_wind (void)
{
    g_autoptr(LrgWeather) weather = NULL;
    gfloat wind_x, wind_y;

    weather = lrg_weather_new ("windy", "Windy");

    lrg_weather_set_wind (weather, 50.0f, 10.0f);
    lrg_weather_get_wind (weather, &wind_x, &wind_y);

    g_assert_cmpfloat (wind_x, ==, 50.0f);
    g_assert_cmpfloat (wind_y, ==, 10.0f);
}

/* ========================================================================== */
/*                         Day/Night Cycle Tests                               */
/* ========================================================================== */

static void
test_day_night_new (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    g_assert_nonnull (cycle);
    g_assert_true (LRG_IS_DAY_NIGHT_CYCLE (cycle));
}

static void
test_day_night_time (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    /* Test normalized time */
    lrg_day_night_cycle_set_time (cycle, 0.5f);
    g_assert_cmpfloat (lrg_day_night_cycle_get_time (cycle), ==, 0.5f);

    /* Test wrapping */
    lrg_day_night_cycle_set_time (cycle, 1.5f);
    g_assert_cmpfloat (lrg_day_night_cycle_get_time (cycle), ==, 0.5f);

    /* Test hours */
    lrg_day_night_cycle_set_hours (cycle, 12.0f);
    g_assert_cmpfloat (lrg_day_night_cycle_get_hours (cycle), ==, 12.0f);
}

static void
test_day_night_time_of_day (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    /* Test night (midnight) */
    lrg_day_night_cycle_set_time (cycle, 0.0f);
    g_assert_cmpint (lrg_day_night_cycle_get_time_of_day (cycle), ==, LRG_TIME_OF_DAY_NIGHT);

    /* Test dawn */
    lrg_day_night_cycle_set_time (cycle, 0.25f);
    g_assert_cmpint (lrg_day_night_cycle_get_time_of_day (cycle), ==, LRG_TIME_OF_DAY_DAWN);

    /* Test noon */
    lrg_day_night_cycle_set_time (cycle, 0.5f);
    g_assert_cmpint (lrg_day_night_cycle_get_time_of_day (cycle), ==, LRG_TIME_OF_DAY_NOON);

    /* Test dusk */
    lrg_day_night_cycle_set_time (cycle, 0.75f);
    g_assert_cmpint (lrg_day_night_cycle_get_time_of_day (cycle), ==, LRG_TIME_OF_DAY_DUSK);
}

static void
test_day_night_day_length (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    lrg_day_night_cycle_set_day_length (cycle, 300.0f);
    g_assert_cmpfloat (lrg_day_night_cycle_get_day_length (cycle), ==, 300.0f);
}

static void
test_day_night_paused (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    g_assert_false (lrg_day_night_cycle_get_paused (cycle));

    lrg_day_night_cycle_set_paused (cycle, TRUE);
    g_assert_true (lrg_day_night_cycle_get_paused (cycle));
}

static void
test_day_night_ambient (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;
    guint8 r, g, b;
    gfloat brightness;

    cycle = lrg_day_night_cycle_new ();

    /* At noon, should be bright */
    lrg_day_night_cycle_set_time (cycle, 0.5f);
    lrg_day_night_cycle_get_ambient_color (cycle, &r, &g, &b);
    brightness = lrg_day_night_cycle_get_ambient_brightness (cycle);

    g_assert_cmpfloat (brightness, >, 0.5f);

    /* At night, should be dark */
    lrg_day_night_cycle_set_time (cycle, 0.0f);
    brightness = lrg_day_night_cycle_get_ambient_brightness (cycle);

    g_assert_cmpfloat (brightness, <, 0.5f);
}

static void
test_day_night_sun_angle (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;
    gfloat angle;

    cycle = lrg_day_night_cycle_new ();

    /* At noon (0.5), sun should be overhead (90 degrees) */
    lrg_day_night_cycle_set_time (cycle, 0.5f);
    angle = lrg_day_night_cycle_get_sun_angle (cycle);
    g_assert_cmpfloat (angle, ==, 90.0f);

    /* At sunrise (0.25), sun at horizon (0 degrees) */
    lrg_day_night_cycle_set_time (cycle, 0.25f);
    angle = lrg_day_night_cycle_get_sun_angle (cycle);
    g_assert_cmpfloat (angle, ==, 0.0f);

    /* At night, sun not visible (-1) */
    lrg_day_night_cycle_set_time (cycle, 0.0f);
    angle = lrg_day_night_cycle_get_sun_angle (cycle);
    g_assert_cmpfloat (angle, ==, -1.0f);
}

static void
test_day_night_update (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;
    gfloat initial_time;

    cycle = lrg_day_night_cycle_new ();

    lrg_day_night_cycle_set_time (cycle, 0.0f);
    lrg_day_night_cycle_set_day_length (cycle, 100.0f);  /* 100 second day */

    initial_time = lrg_day_night_cycle_get_time (cycle);

    lrg_day_night_cycle_update (cycle, 10.0f);  /* 10 seconds = 0.1 day */

    g_assert_cmpfloat (lrg_day_night_cycle_get_time (cycle), >, initial_time);
    g_assert_cmpfloat (lrg_day_night_cycle_get_time (cycle), ==, 0.1f);
}

static void
test_day_night_colors (void)
{
    g_autoptr(LrgDayNightCycle) cycle = NULL;

    cycle = lrg_day_night_cycle_new ();

    /* Just ensure these don't crash */
    lrg_day_night_cycle_set_dawn_color (cycle, 255, 180, 100);
    lrg_day_night_cycle_set_day_color (cycle, 255, 255, 255);
    lrg_day_night_cycle_set_dusk_color (cycle, 255, 140, 80);
    lrg_day_night_cycle_set_night_color (cycle, 40, 40, 80);
}

/* ========================================================================== */
/*                        Weather Manager Tests                                */
/* ========================================================================== */

static void
test_manager_new (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;

    manager = lrg_weather_manager_new ();

    g_assert_nonnull (manager);
    g_assert_true (LRG_IS_WEATHER_MANAGER (manager));
}

static void
test_manager_register_weather (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    g_autoptr(LrgWeather) weather = NULL;
    GList *list;

    manager = lrg_weather_manager_new ();
    weather = lrg_weather_new ("clear", "Clear");

    lrg_weather_manager_register_weather (manager, weather);

    g_assert_nonnull (lrg_weather_manager_get_weather (manager, "clear"));

    list = lrg_weather_manager_get_registered_weather (manager);
    g_assert_cmpuint (g_list_length (list), ==, 1);
    g_list_free (list);
}

static void
test_manager_set_weather (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    g_autoptr(LrgWeather) weather = NULL;

    manager = lrg_weather_manager_new ();
    weather = lrg_weather_new ("rainy", "Rainy");

    lrg_weather_manager_register_weather (manager, weather);

    g_assert_null (lrg_weather_manager_get_active_weather (manager));

    lrg_weather_manager_set_weather (manager, "rainy", 0.0f);

    g_assert_nonnull (lrg_weather_manager_get_active_weather (manager));
    g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (manager), ==, "rainy");
}

static void
test_manager_clear_weather (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    g_autoptr(LrgWeather) weather = NULL;

    manager = lrg_weather_manager_new ();
    weather = lrg_weather_new ("stormy", "Storm");

    lrg_weather_manager_register_weather (manager, weather);
    lrg_weather_manager_set_weather (manager, "stormy", 0.0f);

    g_assert_nonnull (lrg_weather_manager_get_active_weather (manager));

    lrg_weather_manager_clear_weather (manager, 0.0f);

    g_assert_null (lrg_weather_manager_get_active_weather (manager));
}

static void
test_manager_wind (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    gfloat wind_x, wind_y;

    manager = lrg_weather_manager_new ();

    lrg_weather_manager_set_wind (manager, 30.0f, 5.0f);
    lrg_weather_manager_get_wind (manager, &wind_x, &wind_y);

    g_assert_cmpfloat (wind_x, ==, 30.0f);
    g_assert_cmpfloat (wind_y, ==, 5.0f);
}

static void
test_manager_day_night_cycle (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    LrgDayNightCycle *cycle;

    manager = lrg_weather_manager_new ();

    cycle = lrg_weather_manager_get_day_night_cycle (manager);
    g_assert_nonnull (cycle);
    g_assert_true (LRG_IS_DAY_NIGHT_CYCLE (cycle));

    g_assert_true (lrg_weather_manager_get_day_night_enabled (manager));

    lrg_weather_manager_set_day_night_enabled (manager, FALSE);
    g_assert_false (lrg_weather_manager_get_day_night_enabled (manager));
}

static void
test_manager_combined_ambient (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    guint8 r, g, b;
    gfloat brightness;

    manager = lrg_weather_manager_new ();

    lrg_weather_manager_get_combined_ambient (manager, &r, &g, &b, &brightness);

    /* Should return some valid values */
    g_assert_cmpuint (r, <=, 255);
    g_assert_cmpuint (g, <=, 255);
    g_assert_cmpuint (b, <=, 255);
    g_assert_cmpfloat (brightness, >=, 0.0f);
}

static void
test_manager_transition (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    g_autoptr(LrgWeather) weather1 = NULL;
    g_autoptr(LrgWeather) weather2 = NULL;

    manager = lrg_weather_manager_new ();
    weather1 = lrg_weather_new ("clear", "Clear");
    weather2 = lrg_weather_new ("rainy", "Rainy");

    lrg_weather_manager_register_weather (manager, weather1);
    lrg_weather_manager_register_weather (manager, weather2);

    lrg_weather_manager_set_weather (manager, "clear", 0.0f);

    g_assert_false (lrg_weather_manager_is_transitioning (manager));

    /* Start transition */
    lrg_weather_manager_set_weather (manager, "rainy", 2.0f);

    g_assert_true (lrg_weather_manager_is_transitioning (manager));

    /* Update to complete transition */
    lrg_weather_manager_update (manager, 2.5f);

    g_assert_false (lrg_weather_manager_is_transitioning (manager));
    g_assert_cmpstr (lrg_weather_manager_get_active_weather_id (manager), ==, "rainy");
}

static void
test_manager_update (void)
{
    g_autoptr(LrgWeatherManager) manager = NULL;
    g_autoptr(LrgWeather) weather = NULL;

    manager = lrg_weather_manager_new ();
    weather = lrg_weather_new ("test", "Test");

    lrg_weather_manager_register_weather (manager, weather);
    lrg_weather_manager_set_weather (manager, "test", 0.0f);

    /* Just ensure update doesn't crash */
    lrg_weather_manager_update (manager, 0.016f);
    lrg_weather_manager_update (manager, 0.016f);
}

/* ========================================================================== */
/*                           Weather Effect Base Tests                         */
/* ========================================================================== */

static void
test_effect_intensity (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    /* Default intensity is 0.5f */
    g_assert_cmpfloat (lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (rain)), ==, 0.5f);

    lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 1.0f);
    g_assert_cmpfloat (lrg_weather_effect_get_intensity (LRG_WEATHER_EFFECT (rain)), ==, 1.0f);
}

static void
test_effect_active (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    g_assert_false (lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (rain)));

    lrg_weather_effect_activate (LRG_WEATHER_EFFECT (rain));
    g_assert_true (lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (rain)));

    lrg_weather_effect_deactivate (LRG_WEATHER_EFFECT (rain));
    g_assert_false (lrg_weather_effect_is_active (LRG_WEATHER_EFFECT (rain)));
}

static void
test_effect_wind (void)
{
    g_autoptr(LrgRain) rain = NULL;
    gfloat wind_x, wind_y;

    rain = lrg_rain_new ();

    lrg_weather_effect_set_wind (LRG_WEATHER_EFFECT (rain), 20.0f, 5.0f);
    lrg_weather_effect_get_wind (LRG_WEATHER_EFFECT (rain), &wind_x, &wind_y);

    g_assert_cmpfloat (wind_x, ==, 20.0f);
    g_assert_cmpfloat (wind_y, ==, 5.0f);
}

static void
test_effect_render_layer (void)
{
    g_autoptr(LrgRain) rain = NULL;

    rain = lrg_rain_new ();

    lrg_weather_effect_set_render_layer (LRG_WEATHER_EFFECT (rain), 5);
    g_assert_cmpint (lrg_weather_effect_get_render_layer (LRG_WEATHER_EFFECT (rain)), ==, 5);
}

/* ========================================================================== */
/*                              Main Entry Point                               */
/* ========================================================================== */

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);

    /* Rain tests */
    g_test_add_func ("/weather/rain/new", test_rain_new);
    g_test_add_func ("/weather/rain/properties", test_rain_properties);
    g_test_add_func ("/weather/rain/splash", test_rain_splash);
    g_test_add_func ("/weather/rain/color", test_rain_color);

    /* Snow tests */
    g_test_add_func ("/weather/snow/new", test_snow_new);
    g_test_add_func ("/weather/snow/properties", test_snow_properties);
    g_test_add_func ("/weather/snow/sway", test_snow_sway);
    g_test_add_func ("/weather/snow/accumulation", test_snow_accumulation);

    /* Fog tests */
    g_test_add_func ("/weather/fog/new", test_fog_new);
    g_test_add_func ("/weather/fog/type", test_fog_type);
    g_test_add_func ("/weather/fog/density", test_fog_density);
    g_test_add_func ("/weather/fog/distance", test_fog_distance);
    g_test_add_func ("/weather/fog/animation", test_fog_animation);

    /* Lightning tests */
    g_test_add_func ("/weather/lightning/new", test_lightning_new);
    g_test_add_func ("/weather/lightning/interval", test_lightning_interval);
    g_test_add_func ("/weather/lightning/flash", test_lightning_flash);
    g_test_add_func ("/weather/lightning/thunder", test_lightning_thunder);
    g_test_add_func ("/weather/lightning/trigger", test_lightning_trigger);

    /* Weather state tests */
    g_test_add_func ("/weather/state/new", test_weather_new);
    g_test_add_func ("/weather/state/active", test_weather_active);
    g_test_add_func ("/weather/state/effects", test_weather_effects);
    g_test_add_func ("/weather/state/ambient", test_weather_ambient);
    g_test_add_func ("/weather/state/wind", test_weather_wind);

    /* Day/night cycle tests */
    g_test_add_func ("/weather/day-night/new", test_day_night_new);
    g_test_add_func ("/weather/day-night/time", test_day_night_time);
    g_test_add_func ("/weather/day-night/time-of-day", test_day_night_time_of_day);
    g_test_add_func ("/weather/day-night/day-length", test_day_night_day_length);
    g_test_add_func ("/weather/day-night/paused", test_day_night_paused);
    g_test_add_func ("/weather/day-night/ambient", test_day_night_ambient);
    g_test_add_func ("/weather/day-night/sun-angle", test_day_night_sun_angle);
    g_test_add_func ("/weather/day-night/update", test_day_night_update);
    g_test_add_func ("/weather/day-night/colors", test_day_night_colors);

    /* Weather manager tests */
    g_test_add_func ("/weather/manager/new", test_manager_new);
    g_test_add_func ("/weather/manager/register", test_manager_register_weather);
    g_test_add_func ("/weather/manager/set-weather", test_manager_set_weather);
    g_test_add_func ("/weather/manager/clear-weather", test_manager_clear_weather);
    g_test_add_func ("/weather/manager/wind", test_manager_wind);
    g_test_add_func ("/weather/manager/day-night-cycle", test_manager_day_night_cycle);
    g_test_add_func ("/weather/manager/combined-ambient", test_manager_combined_ambient);
    g_test_add_func ("/weather/manager/transition", test_manager_transition);
    g_test_add_func ("/weather/manager/update", test_manager_update);

    /* Weather effect base tests */
    g_test_add_func ("/weather/effect/intensity", test_effect_intensity);
    g_test_add_func ("/weather/effect/active", test_effect_active);
    g_test_add_func ("/weather/effect/wind", test_effect_wind);
    g_test_add_func ("/weather/effect/render-layer", test_effect_render_layer);

    return g_test_run ();
}
