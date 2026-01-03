# Object Pooling System

The object pooling system provides efficient reuse of frequently allocated objects, reducing garbage collection pressure and improving performance in games with many short-lived objects like bullets, particles, or effects.

## Components

| Type | Description |
|------|-------------|
| `LrgObjectPool` | Generic object pool manager |
| `LrgPoolable` | Interface for poolable objects |

## Overview

Object pooling pre-allocates a set of objects and reuses them instead of creating and destroying objects repeatedly. This is essential for games that spawn many temporary objects like:

- Bullets and projectiles
- Particle effects
- Sound effect instances
- Enemy spawns
- Collectibles

## LrgPoolable Interface

Objects that can be pooled must implement the `LrgPoolable` interface:

```c
#define MY_TYPE_BULLET (my_bullet_get_type ())
G_DECLARE_FINAL_TYPE (MyBullet, my_bullet, MY, BULLET, GObject)

struct _MyBullet
{
    GObject parent_instance;
    gfloat x, y;
    gfloat velocity_x, velocity_y;
    gboolean active;
};

/* Implement LrgPoolable interface */
static void
my_bullet_reset (LrgPoolable *poolable)
{
    MyBullet *self = MY_BULLET (poolable);
    self->x = 0.0f;
    self->y = 0.0f;
    self->velocity_x = 0.0f;
    self->velocity_y = 0.0f;
}

static gboolean
my_bullet_is_active (LrgPoolable *poolable)
{
    return MY_BULLET (poolable)->active;
}

static void
my_bullet_set_active (LrgPoolable *poolable, gboolean active)
{
    MY_BULLET (poolable)->active = active;
}

static void
my_bullet_poolable_init (LrgPoolableInterface *iface)
{
    iface->reset = my_bullet_reset;
    iface->is_active = my_bullet_is_active;
    iface->set_active = my_bullet_set_active;
}

G_DEFINE_TYPE_WITH_CODE (MyBullet, my_bullet, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (LRG_TYPE_POOLABLE, my_bullet_poolable_init))
```

### Interface Methods

```c
/**
 * LrgPoolableInterface:
 * @reset: Reset object to initial state when returned to pool
 * @is_active: Check if object is currently in use
 * @set_active: Mark object as active or inactive
 */
struct _LrgPoolableInterface
{
    GTypeInterface parent_iface;

    void     (*reset)      (LrgPoolable *self);
    gboolean (*is_active)  (LrgPoolable *self);
    void     (*set_active) (LrgPoolable *self,
                            gboolean     active);
};
```

## LrgObjectPool

### Construction

```c
/* Create pool for a specific type */
LrgObjectPool *pool = lrg_object_pool_new (MY_TYPE_BULLET);

/* Create with initial capacity */
LrgObjectPool *pool = lrg_object_pool_new_with_capacity (MY_TYPE_BULLET, 100);
```

### Configuration

```c
/* Growth policy when pool is exhausted */
typedef enum {
    LRG_POOL_GROWTH_NONE,     /* Don't grow, return NULL */
    LRG_POOL_GROWTH_DOUBLE,   /* Double capacity */
    LRG_POOL_GROWTH_LINEAR,   /* Add fixed amount */
    LRG_POOL_GROWTH_UNLIMITED /* Grow as needed */
} LrgPoolGrowthPolicy;

lrg_object_pool_set_growth_policy (pool, LRG_POOL_GROWTH_DOUBLE);
lrg_object_pool_set_growth_amount (pool, 10);  /* For LINEAR policy */

/* Set maximum capacity (0 = unlimited) */
lrg_object_pool_set_max_capacity (pool, 500);
guint max = lrg_object_pool_get_max_capacity (pool);

/* Prewarm the pool (allocate objects upfront) */
lrg_object_pool_prewarm (pool, 50);
```

### Acquiring and Releasing Objects

```c
/* Get an object from the pool */
LrgPoolable *obj = lrg_object_pool_acquire (pool);
if (obj != NULL)
{
    MyBullet *bullet = MY_BULLET (obj);
    bullet->x = spawn_x;
    bullet->y = spawn_y;
    bullet->velocity_x = dir_x * speed;
    bullet->velocity_y = dir_y * speed;
}

/* Return object to pool when done */
lrg_object_pool_release (pool, obj);
```

### Iteration

```c
/* Iterate over all active objects */
lrg_object_pool_foreach_active (pool, update_bullet, delta_ptr);

static void
update_bullet (LrgPoolable *poolable, gpointer user_data)
{
    MyBullet *bullet = MY_BULLET (poolable);
    gfloat delta = *(gfloat *)user_data;

    bullet->x += bullet->velocity_x * delta;
    bullet->y += bullet->velocity_y * delta;

    /* Check if bullet should be deactivated */
    if (is_out_of_bounds (bullet))
        lrg_poolable_set_active (poolable, FALSE);
}
```

### Pool Statistics

```c
/* Query pool state */
guint total = lrg_object_pool_get_capacity (pool);
guint active = lrg_object_pool_get_active_count (pool);
guint available = lrg_object_pool_get_available_count (pool);
```

## Complete Example: Bullet Pool

```c
typedef struct {
    LrgObjectPool *bullet_pool;
    LrgObjectPool *explosion_pool;
} ShooterGame;

static void
shooter_game_init (ShooterGame *self)
{
    /* Create bullet pool with 100 preallocated bullets */
    self->bullet_pool = lrg_object_pool_new_with_capacity (MY_TYPE_BULLET, 100);
    lrg_object_pool_set_growth_policy (self->bullet_pool, LRG_POOL_GROWTH_DOUBLE);
    lrg_object_pool_set_max_capacity (self->bullet_pool, 500);
    lrg_object_pool_prewarm (self->bullet_pool, 100);

    /* Create explosion pool */
    self->explosion_pool = lrg_object_pool_new_with_capacity (MY_TYPE_EXPLOSION, 20);
    lrg_object_pool_prewarm (self->explosion_pool, 20);
}

static void
shooter_game_fire_bullet (ShooterGame *self,
                          gfloat       x,
                          gfloat       y,
                          gfloat       angle)
{
    LrgPoolable *poolable = lrg_object_pool_acquire (self->bullet_pool);
    if (poolable == NULL)
    {
        /* Pool exhausted and can't grow further */
        return;
    }

    MyBullet *bullet = MY_BULLET (poolable);
    bullet->x = x;
    bullet->y = y;
    bullet->velocity_x = cosf (angle) * BULLET_SPEED;
    bullet->velocity_y = sinf (angle) * BULLET_SPEED;
}

static void
update_bullet_cb (LrgPoolable *poolable, gpointer user_data)
{
    ShooterGame *game = user_data;
    MyBullet *bullet = MY_BULLET (poolable);

    /* Move bullet */
    bullet->x += bullet->velocity_x * game->delta;
    bullet->y += bullet->velocity_y * game->delta;

    /* Check bounds */
    if (bullet->x < 0 || bullet->x > SCREEN_WIDTH ||
        bullet->y < 0 || bullet->y > SCREEN_HEIGHT)
    {
        lrg_object_pool_release (game->bullet_pool, poolable);
        return;
    }

    /* Check collision */
    Enemy *hit = check_enemy_collision (game, bullet->x, bullet->y);
    if (hit != NULL)
    {
        spawn_explosion (game, bullet->x, bullet->y);
        enemy_take_damage (hit, BULLET_DAMAGE);
        lrg_object_pool_release (game->bullet_pool, poolable);
    }
}

static void
shooter_game_update (ShooterGame *self, gfloat delta)
{
    self->delta = delta;

    /* Update all active bullets */
    lrg_object_pool_foreach_active (self->bullet_pool, update_bullet_cb, self);

    /* Update all active explosions */
    lrg_object_pool_foreach_active (self->explosion_pool, update_explosion_cb, self);
}

static void
draw_bullet_cb (LrgPoolable *poolable, gpointer user_data)
{
    MyBullet *bullet = MY_BULLET (poolable);
    g_autoptr(GrlColor) yellow = grl_color_new (255, 255, 0, 255);
    grl_draw_circle (bullet->x, bullet->y, 4.0f, yellow);
}

static void
shooter_game_draw (ShooterGame *self)
{
    /* Draw all active bullets */
    lrg_object_pool_foreach_active (self->bullet_pool, draw_bullet_cb, NULL);
}
```

## Performance Guidelines

### When to Use Pooling

| Scenario | Use Pooling? | Reason |
|----------|--------------|--------|
| Bullets (hundreds/frame) | Yes | High allocation frequency |
| Particles | Yes | Very high frequency |
| Player character | No | Single long-lived object |
| UI elements | Maybe | Depends on frequency |
| Level geometry | No | Long-lived, loaded once |
| Sound effects | Yes | Many short instances |

### Pool Sizing

```c
/* Start with estimated peak usage + 20% buffer */
guint estimated_peak = 100;
guint buffer = estimated_peak * 0.2;
lrg_object_pool_new_with_capacity (type, estimated_peak + buffer);

/* Monitor and adjust */
static void
debug_pool_usage (LrgObjectPool *pool)
{
    guint active = lrg_object_pool_get_active_count (pool);
    guint capacity = lrg_object_pool_get_capacity (pool);
    g_print ("Pool: %u/%u (%.1f%%)\n", active, capacity,
             (gfloat)active / capacity * 100.0f);
}
```

### Growth Policies

| Policy | Use Case | Trade-off |
|--------|----------|-----------|
| `NONE` | Fixed maximum (e.g., max bullets) | Predictable, may drop objects |
| `DOUBLE` | General purpose | Good balance |
| `LINEAR` | Gradual growth | Less memory spikes |
| `UNLIMITED` | Must never fail | Can consume all memory |

## Thread Safety

`LrgObjectPool` is **not** thread-safe by default. For multi-threaded use:

```c
/* Use mutex for thread safety */
GMutex pool_mutex;

static LrgPoolable *
acquire_thread_safe (LrgObjectPool *pool)
{
    g_mutex_lock (&pool_mutex);
    LrgPoolable *obj = lrg_object_pool_acquire (pool);
    g_mutex_unlock (&pool_mutex);
    return obj;
}
```

## Integration with Templates

Shooter templates include built-in bullet pooling:

```c
/* LrgTwinStickTemplate manages bullet pool automatically */
static void
twin_stick_configure (LrgGameTemplate *template)
{
    LrgTwinStickTemplate *shooter = LRG_TWIN_STICK_TEMPLATE (template);

    /* Configure the built-in bullet pool */
    lrg_twin_stick_template_set_bullet_pool_size (shooter, 200);
    lrg_twin_stick_template_set_bullet_pool_growth (shooter, LRG_POOL_GROWTH_DOUBLE);
}

/* Use template's spawn function */
lrg_twin_stick_template_spawn_bullet (shooter, x, y, angle, speed);
```

## Best Practices

1. **Prewarm at load time**: Allocate objects during loading screens, not during gameplay.

2. **Reset completely**: The `reset()` method must restore the object to a clean initial state.

3. **Size appropriately**: Too small causes runtime allocations; too large wastes memory.

4. **Monitor usage**: Track peak usage during development to size pools correctly.

5. **Clear on level transitions**: Release all objects when changing levels or scenes.

## Related Documentation

- [LrgTwinStickTemplate](../templates/shooter-template.md) - Built-in bullet pooling
- [LrgShmupTemplate](../templates/shooter-template.md) - Bullet pattern pooling
- [Particle System](../../particles/index.md) - Particle-specific pooling
