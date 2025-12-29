# LrgParticlePool

`LrgParticlePool` manages a pre-allocated pool of particles for efficient memory management, avoiding allocation during gameplay.

## Overview

Particle systems can spawn thousands of particles per second. Allocating each particle individually causes performance issues from memory fragmentation and GC pressure. The pool pre-allocates particles and recycles them.

## Type

```c
#define LRG_TYPE_PARTICLE_POOL (lrg_particle_pool_get_type ())
G_DECLARE_FINAL_TYPE (LrgParticlePool, lrg_particle_pool, LRG, PARTICLE_POOL, GObject)
```

## Functions

### Creation

```c
LrgParticlePool *lrg_particle_pool_new (guint capacity);
```

Creates a pool with the specified maximum capacity.

### Acquire/Release

```c
LrgParticle *lrg_particle_pool_acquire (LrgParticlePool *pool);
void lrg_particle_pool_release (LrgParticlePool *pool, LrgParticle *particle);
```

- `acquire` returns a particle from the pool (or NULL if exhausted)
- `release` returns a particle to the pool for reuse

### Management

```c
void lrg_particle_pool_clear (LrgParticlePool *pool);
guint lrg_particle_pool_get_capacity (LrgParticlePool *pool);
guint lrg_particle_pool_get_available (LrgParticlePool *pool);
guint lrg_particle_pool_get_active (LrgParticlePool *pool);
```

## Usage

```c
/* Create pool with 1000 particle capacity */
g_autoptr(LrgParticlePool) pool = lrg_particle_pool_new (1000);

/* Acquire a particle */
LrgParticle *p = lrg_particle_pool_acquire (pool);
if (p != NULL)
{
    /* Use particle... */
    lrg_particle_set_position (p, x, y, 0.0f);
}

/* Release when particle dies */
if (!lrg_particle_is_alive (p))
{
    lrg_particle_pool_release (pool, p);
}

/* Check pool status */
g_print ("Active: %u / %u\n",
         lrg_particle_pool_get_active (pool),
         lrg_particle_pool_get_capacity (pool));
```

## Pool Exhaustion

When the pool is exhausted, `acquire` returns NULL. Handle this gracefully:

```c
LrgParticle *p = lrg_particle_pool_acquire (pool);
if (p == NULL)
{
    /* Pool exhausted - skip this particle */
    return;
}
```

## Capacity Guidelines

| Effect Type | Recommended Capacity |
|-------------|---------------------|
| Ambient (dust, sparkles) | 100-500 |
| Explosions | 500-2000 |
| Weather (rain, snow) | 2000-5000 |
| Heavy effects | 5000-10000 |

## Integration with LrgParticleSystem

`LrgParticleSystem` internally uses a pool. You typically don't need to manage pools directly:

```c
LrgParticleSystem *system = lrg_particle_system_new_with_capacity (2000);
/* Pool is managed internally */
```
