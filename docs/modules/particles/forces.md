# LrgParticleForce

`LrgParticleForce` is the base class for forces that affect particle movement. Multiple force types are provided for different effects.

## Base Type

```c
#define LRG_TYPE_PARTICLE_FORCE (lrg_particle_force_get_type ())
G_DECLARE_DERIVABLE_TYPE (LrgParticleForce, lrg_particle_force, LRG, PARTICLE_FORCE, GObject)
```

### Base Methods

```c
void lrg_particle_force_apply (LrgParticleForce *force,
                               LrgParticle *particle,
                               gfloat delta_time);
void lrg_particle_force_set_enabled (LrgParticleForce *force, gboolean enabled);
gboolean lrg_particle_force_is_enabled (LrgParticleForce *force);
void lrg_particle_force_set_strength (LrgParticleForce *force, gfloat strength);
gfloat lrg_particle_force_get_strength (LrgParticleForce *force);
```

## Force Types

### Gravity

Applies constant directional acceleration (e.g., downward pull).

```c
#define LRG_TYPE_PARTICLE_FORCE_GRAVITY (lrg_particle_force_gravity_get_type ())

LrgParticleForceGravity *lrg_particle_force_gravity_new (gfloat gx, gfloat gy, gfloat gz);
void lrg_particle_force_gravity_set_direction (LrgParticleForceGravity *force,
                                               gfloat gx, gfloat gy, gfloat gz);
```

**Example:**
```c
/* Standard downward gravity */
LrgParticleForce *gravity = lrg_particle_force_gravity_new (0.0f, 200.0f, 0.0f);

/* Reverse gravity (rising) */
LrgParticleForce *rise = lrg_particle_force_gravity_new (0.0f, -50.0f, 0.0f);
```

### Wind

Applies directional force with optional noise variation for natural wind effects.

```c
#define LRG_TYPE_PARTICLE_FORCE_WIND (lrg_particle_force_wind_get_type ())

LrgParticleForceWind *lrg_particle_force_wind_new (gfloat wx, gfloat wy, gfloat wz);
void lrg_particle_force_wind_set_direction (LrgParticleForceWind *force,
                                            gfloat wx, gfloat wy, gfloat wz);
void lrg_particle_force_wind_set_turbulence (LrgParticleForceWind *force, gfloat amount);
void lrg_particle_force_wind_set_frequency (LrgParticleForceWind *force, gfloat freq);
```

**Example:**
```c
/* Gentle breeze with variation */
LrgParticleForce *wind = lrg_particle_force_wind_new (30.0f, 0.0f, 0.0f);
lrg_particle_force_wind_set_turbulence (LRG_PARTICLE_FORCE_WIND (wind), 0.3f);
```

### Attractor

Point-based attraction or repulsion. Particles are pulled toward or pushed away from a point.

```c
#define LRG_TYPE_PARTICLE_FORCE_ATTRACTOR (lrg_particle_force_attractor_get_type ())

LrgParticleForceAttractor *lrg_particle_force_attractor_new (gfloat x, gfloat y, gfloat z,
                                                              gfloat strength);
void lrg_particle_force_attractor_set_position (LrgParticleForceAttractor *force,
                                                gfloat x, gfloat y, gfloat z);
void lrg_particle_force_attractor_set_radius (LrgParticleForceAttractor *force, gfloat radius);
void lrg_particle_force_attractor_set_falloff (LrgParticleForceAttractor *force,
                                               LrgForceFalloff falloff);
```

**Falloff Types:**
- `LRG_FORCE_FALLOFF_NONE` - Constant force
- `LRG_FORCE_FALLOFF_LINEAR` - Force decreases linearly with distance
- `LRG_FORCE_FALLOFF_SQUARED` - Force decreases with distance squared (realistic)

**Example:**
```c
/* Black hole attractor */
LrgParticleForce *attractor = lrg_particle_force_attractor_new (400.0f, 300.0f, 0.0f, 500.0f);
lrg_particle_force_attractor_set_radius (LRG_PARTICLE_FORCE_ATTRACTOR (attractor), 200.0f);

/* Repulsor (negative strength) */
LrgParticleForce *repulsor = lrg_particle_force_attractor_new (400.0f, 300.0f, 0.0f, -300.0f);
```

### Turbulence

Perlin noise-based chaotic movement for natural-looking effects.

```c
#define LRG_TYPE_PARTICLE_FORCE_TURBULENCE (lrg_particle_force_turbulence_get_type ())

LrgParticleForceTurbulence *lrg_particle_force_turbulence_new (gfloat strength);
void lrg_particle_force_turbulence_set_scale (LrgParticleForceTurbulence *force, gfloat scale);
void lrg_particle_force_turbulence_set_speed (LrgParticleForceTurbulence *force, gfloat speed);
void lrg_particle_force_turbulence_set_octaves (LrgParticleForceTurbulence *force, guint octaves);
```

**Example:**
```c
/* Smoky turbulence */
LrgParticleForce *turbulence = lrg_particle_force_turbulence_new (50.0f);
lrg_particle_force_turbulence_set_scale (LRG_PARTICLE_FORCE_TURBULENCE (turbulence), 0.01f);
lrg_particle_force_turbulence_set_speed (LRG_PARTICLE_FORCE_TURBULENCE (turbulence), 2.0f);
```

## Combining Forces

Add multiple forces to a particle system for complex behavior:

```c
LrgParticleSystem *system = lrg_particle_system_new ();

/* Add gravity */
lrg_particle_system_add_force (system, lrg_particle_force_gravity_new (0.0f, 100.0f, 0.0f));

/* Add wind */
lrg_particle_system_add_force (system, lrg_particle_force_wind_new (20.0f, 0.0f, 0.0f));

/* Add subtle turbulence */
lrg_particle_system_add_force (system, lrg_particle_force_turbulence_new (15.0f));
```

## Custom Forces

Subclass `LrgParticleForce` to create custom forces:

```c
struct _MyCustomForce
{
    LrgParticleForce parent;
    /* Custom fields */
};

static void
my_custom_force_apply (LrgParticleForce *force,
                       LrgParticle *particle,
                       gfloat delta_time)
{
    /* Apply custom force logic */
}
```
