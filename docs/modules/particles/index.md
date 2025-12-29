# Particle System Module

The Particle System module provides a flexible and performant particle effects system for visual effects like fire, smoke, explosions, magic, and ambient particles.

## Overview

The particle system follows a producer-consumer pattern with object pooling for performance:

- **LrgParticle** - Individual particle data (position, velocity, life, color, size)
- **LrgParticlePool** - Pre-allocated pool of particles for efficient memory management
- **LrgParticleEmitter** - Spawns particles with configurable shapes and properties
- **LrgParticleForce** - Modifies particle behavior (gravity, wind, attractors, turbulence)
- **LrgParticleSystem** - Coordinates emitters, forces, and rendering

## Quick Start

```c
/* Create a particle system */
LrgParticleSystem *system = lrg_particle_system_new ();

/* Create an emitter */
LrgParticleEmitter *emitter = lrg_particle_emitter_new ();
lrg_particle_emitter_set_rate (emitter, 50.0f);  /* 50 particles/second */
lrg_particle_emitter_set_lifetime (emitter, 2.0f);
lrg_particle_emitter_set_position (emitter, 400.0f, 300.0f, 0.0f);

/* Add gravity force */
LrgParticleForce *gravity = lrg_particle_force_gravity_new (0.0f, 100.0f, 0.0f);
lrg_particle_system_add_force (system, gravity);

/* Add emitter to system */
lrg_particle_system_add_emitter (system, emitter);

/* In game loop */
lrg_particle_system_update (system, delta_time);
lrg_particle_system_draw (system);
```

## Key Concepts

### Particle Lifecycle

1. **Birth** - Emitter spawns particle with initial properties
2. **Life** - Forces modify velocity; particle ages toward death
3. **Death** - Particle reaches end of lifetime; returned to pool

### Emission Shapes

- **Point** - All particles spawn at a single point
- **Circle** - Spawn randomly within a circle
- **Rectangle** - Spawn randomly within a rectangle
- **Sphere** - Spawn randomly within a sphere (3D)
- **Cone** - Spawn with directional spread

### Force Types

| Force | Description |
|-------|-------------|
| Gravity | Constant directional acceleration |
| Wind | Directional force with noise variation |
| Attractor | Point-based attraction or repulsion |
| Turbulence | Perlin noise-based chaotic movement |

## Performance Tips

- Use object pooling (LrgParticlePool) for many particles
- Limit active particle count based on hardware
- Use texture atlases for varied particle appearances
- Consider level-of-detail for distant effects

## Files

| File | Description |
|------|-------------|
| [particle.md](particle.md) | LrgParticle GBoxed type |
| [pool.md](pool.md) | LrgParticlePool memory management |
| [emitter.md](emitter.md) | LrgParticleEmitter configuration |
| [forces.md](forces.md) | LrgParticleForce implementations |
| [system.md](system.md) | LrgParticleSystem coordination |
