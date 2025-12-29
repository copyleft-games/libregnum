# 2D Lighting System Module

The 2D Lighting System provides dynamic lights, shadows, and baked lighting for atmospheric 2D games with support for point lights, spot lights, directional lights, and shadow casting.

## Overview

The lighting system renders lights to a texture that composites with the scene:

- **LrgShadowCaster** - Interface for objects that cast shadows
- **LrgLight2D** - Abstract base class for 2D lights (derivable)
- **LrgPointLight2D** - Radial light with optional flicker
- **LrgSpotLight2D** - Directional cone light
- **LrgDirectionalLight2D** - Sun-like parallel light
- **LrgShadowMap** - Shadow texture renderer
- **LrgLightmap** - Pre-baked static lighting
- **LrgLightProbe** - Samples ambient lighting
- **LrgLightingManager** - Coordinates all lighting

## Quick Start

```c
/* Get the lighting manager from engine */
LrgLightingManager *lighting = lrg_engine_get_lighting_manager (engine);

/* Create a point light */
LrgPointLight2D *torch = lrg_point_light2d_new ();
lrg_light2d_set_position (LRG_LIGHT2D (torch), 200.0f, 150.0f);
lrg_light2d_set_color (LRG_LIGHT2D (torch), 255, 200, 100);
lrg_light2d_set_intensity (LRG_LIGHT2D (torch), 1.5f);
lrg_point_light2d_set_radius (torch, 150.0f);
lrg_point_light2d_set_flicker_enabled (torch, TRUE);

/* Add to manager */
lrg_lighting_manager_add_light (lighting, LRG_LIGHT2D (torch));

/* In game loop */
lrg_lighting_manager_update (lighting, delta_time);
lrg_lighting_manager_render (lighting);
```

## Light Types

### Point Light

Radiates light in all directions from a point:

```c
LrgPointLight2D *light = lrg_point_light2d_new ();
lrg_light2d_set_position (LRG_LIGHT2D (light), x, y);
lrg_light2d_set_color (LRG_LIGHT2D (light), 255, 200, 150);
lrg_light2d_set_intensity (LRG_LIGHT2D (light), 1.0f);
lrg_point_light2d_set_radius (light, 200.0f);
lrg_point_light2d_set_inner_radius (light, 50.0f);  /* Full brightness zone */

/* Flicker effect for torches/candles */
lrg_point_light2d_set_flicker_enabled (light, TRUE);
lrg_point_light2d_set_flicker_amount (light, 0.2f);
lrg_point_light2d_set_flicker_speed (light, 8.0f);
```

### Spot Light

Directional cone of light:

```c
LrgSpotLight2D *spot = lrg_spot_light2d_new ();
lrg_light2d_set_position (LRG_LIGHT2D (spot), x, y);
lrg_spot_light2d_set_direction (spot, 0.5f);        /* Angle in radians */
lrg_spot_light2d_set_outer_angle (spot, 45.0f);     /* Cone angle */
lrg_spot_light2d_set_inner_angle (spot, 30.0f);     /* Full brightness cone */
lrg_spot_light2d_set_radius (spot, 300.0f);
```

### Directional Light

Parallel rays like sunlight:

```c
LrgDirectionalLight2D *sun = lrg_directional_light2d_new ();
lrg_light2d_set_color (LRG_LIGHT2D (sun), 255, 250, 220);
lrg_directional_light2d_set_direction (sun, 2.356f);  /* Angle in radians (~135 degrees) */
lrg_directional_light2d_set_shadow_length (sun, 100.0f);
```

## Light Properties

### Falloff Types

| Falloff | Description |
|---------|-------------|
| `NONE` | No falloff, constant brightness |
| `LINEAR` | Linear decrease with distance |
| `QUADRATIC` | Realistic inverse-square falloff |

```c
lrg_light2d_set_falloff (light, LRG_LIGHT_FALLOFF_QUADRATIC);
```

### Blend Modes

| Mode | Description |
|------|-------------|
| `MULTIPLY` | Multiply with scene (standard) |
| `ADDITIVE` | Add to scene (glowing lights) |
| `SOFT` | Soft blend for subtle lighting |

```c
lrg_lighting_manager_set_blend_mode (lighting, LRG_LIGHT_BLEND_MULTIPLY);
```

## Shadow Casting

### Shadow Caster Interface

Objects implement `LrgShadowCaster` to cast shadows:

```c
/* Register a shadow caster */
lrg_lighting_manager_add_shadow_caster (lighting, LRG_SHADOW_CASTER (wall));

/* Enable shadows on a light */
lrg_light2d_set_casts_shadows (light, TRUE);
lrg_light2d_set_shadow_method (light, LRG_SHADOW_METHOD_GEOMETRY);
```

### Shadow Methods

| Method | Description |
|--------|-------------|
| `RAY_CAST` | CPU ray casting, accurate soft shadows |
| `GEOMETRY` | GPU shadow volumes, fast for many casters |

### Shadow Map

```c
LrgShadowMap *shadows = lrg_shadow_map_new (512, 512);
lrg_shadow_map_set_soft_shadows (shadows, TRUE);
lrg_shadow_map_set_softness (shadows, 4.0f);
```

## Baked Lighting (Lightmaps)

For static scenes, pre-compute lighting:

```c
LrgLightmap *lightmap = lrg_lightmap_new (256, 256);

/* Set ambient base */
lrg_lightmap_clear (lightmap, 30, 30, 40);

/* Bake lights */
lrg_lightmap_bake_light (lightmap, static_light, shadow_casters);

/* Apply to rendering */
lrg_lighting_manager_set_lightmap (lighting, lightmap);
```

### Lightmap Pixel Access

```c
/* Set specific pixel */
lrg_lightmap_set_pixel (lightmap, x, y, 255, 200, 150);

/* Get pixel */
guint8 r, g, b;
lrg_lightmap_get_pixel (lightmap, x, y, &r, &g, &b);
```

## Light Probes

Sample ambient lighting at specific points:

```c
LrgLightProbe *probe = lrg_light_probe_new ();
lrg_light_probe_set_position (probe, player_x, player_y);
lrg_light_probe_set_radius (probe, 50.0f);

/* Sample nearby lights */
GList *lights = lrg_lighting_manager_get_lights (lighting);
lrg_light_probe_sample (probe, lights);

/* Get result */
guint8 r, g, b;
gfloat intensity;
lrg_light_probe_get_color (probe, &r, &g, &b);
intensity = lrg_light_probe_get_intensity (probe);
```

## Ambient Lighting

```c
/* Set global ambient */
lrg_lighting_manager_set_ambient_color (lighting, 20, 20, 30);
lrg_lighting_manager_set_ambient_intensity (lighting, 0.2f);
```

## Viewport Culling

```c
/* Set visible area for culling off-screen lights */
lrg_lighting_manager_set_viewport (lighting, camera_x, camera_y, width, height);
```

## YAML Configuration

```yaml
# Point light
type: PointLight2D
position: { x: 200, y: 150 }
color: { r: 255, g: 200, b: 100 }
intensity: 1.5
radius: 150
inner-radius: 30
falloff: quadratic
casts-shadows: true
shadow-method: geometry
flicker:
  enabled: true
  amount: 0.2
  speed: 8.0
```

```yaml
# Spot light
type: SpotLight2D
position: { x: 400, y: 100 }
direction: 1.571  # Angle in radians (90 degrees, pointing down)
color: { r: 255, g: 255, b: 255 }
outer-angle: 45
inner-angle: 30
radius: 300
casts-shadows: true
```

## Integration with Day/Night

```c
/* Connect weather day/night to lighting */
g_signal_connect (day_night_cycle, "time-changed",
                  G_CALLBACK (on_time_changed), lighting);

static void
on_time_changed (LrgDayNightCycle   *cycle,
                 LrgLightingManager *lighting)
{
    guint8 r, g, b;
    lrg_day_night_cycle_get_ambient_color (cycle, &r, &g, &b);
    lrg_lighting_manager_set_ambient_color (lighting, r, g, b);

    gfloat intensity = lrg_day_night_cycle_get_light_intensity (cycle);
    lrg_lighting_manager_set_ambient_intensity (lighting, intensity);
}
```

## Key Classes

| Class | Description |
|-------|-------------|
| `LrgShadowCaster` | Shadow casting interface |
| `LrgLight2D` | Abstract light base (derivable) |
| `LrgPointLight2D` | Radial point light |
| `LrgSpotLight2D` | Directional cone light |
| `LrgDirectionalLight2D` | Sun-like light |
| `LrgShadowMap` | Shadow texture |
| `LrgLightmap` | Baked static lighting |
| `LrgLightProbe` | Ambient sampler |
| `LrgLightingManager` | Lighting coordinator |

## Files

| File | Description |
|------|-------------|
| `src/lighting/lrg-shadow-caster.h` | Shadow interface |
| `src/lighting/lrg-light2d.h` | Base light class |
| `src/lighting/lrg-point-light2d.h` | Point light |
| `src/lighting/lrg-spot-light2d.h` | Spot light |
| `src/lighting/lrg-directional-light2d.h` | Directional light |
| `src/lighting/lrg-shadow-map.h` | Shadow map |
| `src/lighting/lrg-lightmap.h` | Baked lighting |
| `src/lighting/lrg-light-probe.h` | Light probe |
| `src/lighting/lrg-lighting-manager.h` | Manager |
