# Weather System Module

The Weather System provides dynamic weather effects including rain, snow, fog, lightning, day/night cycles, and smooth weather transitions.

## Overview

The weather system coordinates multiple effects for atmospheric immersion:

- **LrgWeatherEffect** - Abstract base for weather effects (derivable)
- **LrgRain** - Particle-based rain with optional splashes
- **LrgSnow** - Particle-based snow with wind drift
- **LrgFog** - Screen-space fog effect (uniform, linear, exponential, height-based)
- **LrgLightning** - Lightning flashes with thunder audio
- **LrgWeather** - Weather state definition combining effects
- **LrgDayNightCycle** - Time-of-day system with color transitions
- **LrgWeatherManager** - Coordinates weather states and transitions

## Quick Start

```c
/* Get the weather manager from engine */
LrgWeatherManager *weather = lrg_engine_get_weather_manager (engine);

/* Create a rainy weather state */
LrgWeather *rainy = lrg_weather_new ("rainy");

LrgRain *rain = lrg_rain_new ();
lrg_weather_effect_set_intensity (LRG_WEATHER_EFFECT (rain), 0.8f);
lrg_rain_set_drop_count (rain, 1000);
lrg_weather_add_effect (rainy, LRG_WEATHER_EFFECT (rain));

LrgFog *fog = lrg_fog_new ();
lrg_fog_set_type (fog, LRG_FOG_TYPE_LINEAR);
lrg_fog_set_density (fog, 0.3f);
lrg_weather_add_effect (rainy, LRG_WEATHER_EFFECT (fog));

/* Register and transition to it */
lrg_weather_manager_register (weather, rainy);
lrg_weather_manager_transition_to (weather, "rainy", 5.0f);

/* In game loop */
lrg_weather_manager_update (weather, delta_time);
```

## Weather Effects

### Rain

```c
LrgRain *rain = lrg_rain_new ();
lrg_rain_set_drop_count (rain, 1000);
lrg_rain_set_drop_speed (rain, 400.0f);
lrg_rain_set_drop_length (rain, 20.0f);
lrg_rain_set_color (rain, 180, 200, 255, 150);
lrg_rain_set_splash_enabled (rain, TRUE);
lrg_rain_set_wind_influence (rain, 0.5f);
```

### Snow

```c
LrgSnow *snow = lrg_snow_new ();
lrg_snow_set_flake_count (snow, 500);
lrg_snow_set_fall_speed (snow, 50.0f);
lrg_snow_set_drift_amount (snow, 30.0f);
lrg_snow_set_flake_size_range (snow, 2.0f, 6.0f);
lrg_snow_set_rotation_enabled (snow, TRUE);
```

### Fog

```c
LrgFog *fog = lrg_fog_new ();
lrg_fog_set_type (fog, LRG_FOG_TYPE_EXPONENTIAL);
lrg_fog_set_density (fog, 0.5f);
lrg_fog_set_color (fog, 200, 200, 210);
lrg_fog_set_start_distance (fog, 100.0f);  /* For linear fog */
lrg_fog_set_end_distance (fog, 500.0f);
```

### Fog Types

| Type | Description |
|------|-------------|
| `UNIFORM` | Constant density everywhere |
| `LINEAR` | Increases linearly with distance |
| `EXPONENTIAL` | Exponential falloff |
| `HEIGHT` | Density based on Y position |

### Lightning

```c
LrgLightning *lightning = lrg_lightning_new ();
lrg_lightning_set_flash_interval_range (lightning, 5.0f, 15.0f);
lrg_lightning_set_flash_duration (lightning, 0.1f);
lrg_lightning_set_thunder_delay_range (lightning, 0.5f, 3.0f);
lrg_lightning_set_intensity (lightning, 1.0f);
lrg_lightning_set_color (lightning, 255, 255, 230);
```

## Day/Night Cycle

```c
LrgDayNightCycle *cycle = lrg_day_night_cycle_new ();

/* Set time (0.0 = midnight, 0.5 = noon) */
lrg_day_night_cycle_set_time (cycle, 0.25f);  /* 6 AM */

/* Configure speed (1.0 = real-time, 60.0 = 1 hour per minute) */
lrg_day_night_cycle_set_speed (cycle, 120.0f);

/* Get current period */
LrgTimePeriod period = lrg_day_night_cycle_get_period (cycle);
/* DAWN, MORNING, NOON, AFTERNOON, DUSK, EVENING, NIGHT, MIDNIGHT */

/* Get ambient color for current time */
guint8 r, g, b;
lrg_day_night_cycle_get_ambient_color (cycle, &r, &g, &b);

/* Get sun angle for shadow direction */
gfloat angle = lrg_day_night_cycle_get_sun_angle (cycle);
```

### Time Periods

| Period | Time Range | Description |
|--------|------------|-------------|
| `MIDNIGHT` | 0.00 - 0.04 | Deep night |
| `NIGHT` | 0.04 - 0.21 | Night time |
| `DAWN` | 0.21 - 0.29 | Sunrise colors |
| `MORNING` | 0.29 - 0.42 | Morning light |
| `NOON` | 0.42 - 0.58 | Full daylight |
| `AFTERNOON` | 0.58 - 0.71 | Afternoon |
| `DUSK` | 0.71 - 0.79 | Sunset colors |
| `EVENING` | 0.79 - 0.96 | Evening |

## Weather Transitions

```c
/* Smooth transition over 10 seconds */
lrg_weather_manager_transition_to (weather, "stormy", 10.0f);

/* Instant change */
lrg_weather_manager_set_weather (weather, "clear");

/* Check transition state */
if (lrg_weather_manager_is_transitioning (weather))
{
    gfloat progress = lrg_weather_manager_get_transition_progress (weather);
}
```

## Wind System

Wind affects weather particles and can integrate with physics:

```c
/* Set global wind */
lrg_weather_manager_set_wind (weather, 50.0f, 0.0f);  /* Wind from left */
lrg_weather_manager_set_wind_strength (weather, 0.5f);

/* Enable physics wind (affects rigid bodies) */
lrg_weather_manager_set_physics_wind_enabled (weather, TRUE);
```

## YAML Configuration

```yaml
# weather/stormy.yaml
type: Weather
id: stormy
name: "Stormy Weather"
effects:
  - type: Rain
    intensity: 1.0
    drop-count: 2000
    drop-speed: 500
    splash-enabled: true

  - type: Fog
    fog-type: exponential
    density: 0.4
    color: { r: 100, g: 100, b: 120 }

  - type: Lightning
    flash-interval: { min: 3.0, max: 10.0 }
    thunder-enabled: true

wind:
  direction: { x: 100, y: 20 }
  strength: 0.7
  gusts-enabled: true
```

## Signals

```c
g_signal_connect (weather_manager, "weather-changed",
                  G_CALLBACK (on_weather_change), NULL);
g_signal_connect (weather_manager, "transition-started",
                  G_CALLBACK (on_transition_start), NULL);
g_signal_connect (weather_manager, "transition-completed",
                  G_CALLBACK (on_transition_done), NULL);

g_signal_connect (day_night, "period-changed",
                  G_CALLBACK (on_time_period), NULL);
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgWeatherEffect` | Abstract effect base (derivable) |
| `LrgRain` | Rain particles |
| `LrgSnow` | Snow particles |
| `LrgFog` | Fog post-effect |
| `LrgLightning` | Lightning flashes |
| `LrgWeather` | Weather state |
| `LrgDayNightCycle` | Time-of-day |
| `LrgWeatherManager` | Weather coordinator |

## Files

| File | Description |
|------|-------------|
| `src/weather/lrg-weather-effect.h` | Base effect class |
| `src/weather/lrg-rain.h` | Rain effect |
| `src/weather/lrg-snow.h` | Snow effect |
| `src/weather/lrg-fog.h` | Fog effect |
| `src/weather/lrg-lightning.h` | Lightning effect |
| `src/weather/lrg-weather.h` | Weather state |
| `src/weather/lrg-day-night-cycle.h` | Day/night cycle |
| `src/weather/lrg-weather-manager.h` | Manager |
