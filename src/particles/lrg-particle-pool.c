/* lrg-particle-pool.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-particle-pool.h"

/**
 * SECTION:lrg-particle-pool
 * @Title: LrgParticlePool
 * @Short_description: Object pool for particles
 *
 * #LrgParticlePool provides efficient memory management for particles.
 * Rather than allocating and freeing individual particles, the pool
 * maintains a contiguous array of particles and tracks which are alive
 * and which are free for reuse.
 *
 * The pool can grow automatically when more particles are needed,
 * or it can be configured with a fixed maximum capacity.
 *
 * Example:
 * |[<!-- language="C" -->
 * g_autoptr(LrgParticlePool) pool = lrg_particle_pool_new (1000);
 *
 * // Acquire and spawn particles
 * for (int i = 0; i < 100; i++)
 * {
 *     LrgParticle *p = lrg_particle_pool_acquire (pool);
 *     if (p != NULL)
 *     {
 *         lrg_particle_spawn (p, rand_x, rand_y, 0.0f, 2.0f);
 *         lrg_particle_set_velocity (p, 0.0f, -50.0f, 0.0f);
 *     }
 * }
 *
 * // Update all particles each frame
 * guint alive = lrg_particle_pool_update_all (pool, delta_time);
 * g_print ("Particles alive: %u\n", alive);
 * ]|
 */

#define DEFAULT_INITIAL_CAPACITY 256

struct _LrgParticlePool
{
    GObject parent_instance;

    LrgParticle       *particles;       /* Contiguous array of particles */
    guint              capacity;        /* Total slots in array */
    guint              alive_count;     /* Number of alive particles */

    /* Free list management using indices */
    guint             *free_indices;    /* Stack of free slot indices */
    guint              free_count;      /* Number of free slots */

    /* Growth settings */
    LrgPoolGrowPolicy  grow_policy;
    guint              max_capacity;    /* 0 = unlimited */
};

G_DEFINE_FINAL_TYPE (LrgParticlePool, lrg_particle_pool, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CAPACITY,
    PROP_ALIVE_COUNT,
    PROP_FREE_COUNT,
    PROP_GROW_POLICY,
    PROP_MAX_CAPACITY,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
lrg_particle_pool_finalize (GObject *object)
{
    LrgParticlePool *self = LRG_PARTICLE_POOL (object);

    g_clear_pointer (&self->particles, g_free);
    g_clear_pointer (&self->free_indices, g_free);

    G_OBJECT_CLASS (lrg_particle_pool_parent_class)->finalize (object);
}

static void
lrg_particle_pool_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgParticlePool *self = LRG_PARTICLE_POOL (object);

    switch (prop_id)
    {
    case PROP_CAPACITY:
        g_value_set_uint (value, self->capacity);
        break;
    case PROP_ALIVE_COUNT:
        g_value_set_uint (value, self->alive_count);
        break;
    case PROP_FREE_COUNT:
        g_value_set_uint (value, self->free_count);
        break;
    case PROP_GROW_POLICY:
        g_value_set_enum (value, self->grow_policy);
        break;
    case PROP_MAX_CAPACITY:
        g_value_set_uint (value, self->max_capacity);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_pool_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgParticlePool *self = LRG_PARTICLE_POOL (object);

    switch (prop_id)
    {
    case PROP_GROW_POLICY:
        self->grow_policy = g_value_get_enum (value);
        break;
    case PROP_MAX_CAPACITY:
        self->max_capacity = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_particle_pool_class_init (LrgParticlePoolClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_particle_pool_finalize;
    object_class->get_property = lrg_particle_pool_get_property;
    object_class->set_property = lrg_particle_pool_set_property;

    /**
     * LrgParticlePool:capacity:
     *
     * The total capacity of the pool (alive + free particles).
     */
    properties[PROP_CAPACITY] =
        g_param_spec_uint ("capacity",
                           "Capacity",
                           "Total particle capacity",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgParticlePool:alive-count:
     *
     * The number of currently alive particles.
     */
    properties[PROP_ALIVE_COUNT] =
        g_param_spec_uint ("alive-count",
                           "Alive Count",
                           "Number of alive particles",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgParticlePool:free-count:
     *
     * The number of free particles available for acquisition.
     */
    properties[PROP_FREE_COUNT] =
        g_param_spec_uint ("free-count",
                           "Free Count",
                           "Number of free particles",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgParticlePool:grow-policy:
     *
     * The policy for growing the pool when full.
     */
    properties[PROP_GROW_POLICY] =
        g_param_spec_enum ("grow-policy",
                           "Grow Policy",
                           "How to handle pool exhaustion",
                           LRG_TYPE_POOL_GROW_POLICY,
                           LRG_POOL_GROW_DOUBLE,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgParticlePool:max-capacity:
     *
     * Maximum capacity (0 = unlimited).
     */
    properties[PROP_MAX_CAPACITY] =
        g_param_spec_uint ("max-capacity",
                           "Max Capacity",
                           "Maximum pool capacity (0 = unlimited)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_particle_pool_init (LrgParticlePool *self)
{
    self->particles = NULL;
    self->capacity = 0;
    self->alive_count = 0;
    self->free_indices = NULL;
    self->free_count = 0;
    self->grow_policy = LRG_POOL_GROW_DOUBLE;
    self->max_capacity = 0;
}

static gboolean
lrg_particle_pool_grow (LrgParticlePool *self,
                        guint            new_capacity)
{
    LrgParticle *new_particles;
    guint       *new_free_indices;
    guint        old_capacity;
    guint        i;

    /* Check max capacity limit */
    if (self->max_capacity > 0 && new_capacity > self->max_capacity)
        new_capacity = self->max_capacity;

    if (new_capacity <= self->capacity)
        return FALSE;

    old_capacity = self->capacity;

    /* Reallocate particle array */
    new_particles = g_renew (LrgParticle, self->particles, new_capacity);
    if (new_particles == NULL)
        return FALSE;

    /* Initialize new particles */
    for (i = old_capacity; i < new_capacity; i++)
    {
        new_particles[i].alive = FALSE;
        new_particles[i].position_x = 0.0f;
        new_particles[i].position_y = 0.0f;
        new_particles[i].position_z = 0.0f;
        new_particles[i].velocity_x = 0.0f;
        new_particles[i].velocity_y = 0.0f;
        new_particles[i].velocity_z = 0.0f;
        new_particles[i].color_r = 1.0f;
        new_particles[i].color_g = 1.0f;
        new_particles[i].color_b = 1.0f;
        new_particles[i].color_a = 1.0f;
        new_particles[i].size = 1.0f;
        new_particles[i].rotation = 0.0f;
        new_particles[i].rotation_velocity = 0.0f;
        new_particles[i].life = 0.0f;
        new_particles[i].max_life = 0.0f;
        new_particles[i].age = 0.0f;
    }

    self->particles = new_particles;

    /* Reallocate free list */
    new_free_indices = g_renew (guint, self->free_indices, new_capacity);
    if (new_free_indices == NULL)
        return FALSE;

    self->free_indices = new_free_indices;

    /* Add new slots to free list (in reverse order for stack behavior) */
    for (i = new_capacity - 1; i >= old_capacity; i--)
    {
        self->free_indices[self->free_count++] = i;
        if (i == old_capacity)
            break;
    }

    self->capacity = new_capacity;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPACITY]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);

    return TRUE;
}

/**
 * lrg_particle_pool_new:
 * @initial_capacity: Initial number of particles to allocate
 *
 * Creates a new particle pool with the specified initial capacity.
 *
 * Returns: (transfer full): A new #LrgParticlePool
 */
LrgParticlePool *
lrg_particle_pool_new (guint initial_capacity)
{
    return lrg_particle_pool_new_with_policy (initial_capacity,
                                               LRG_POOL_GROW_DOUBLE,
                                               0);
}

/**
 * lrg_particle_pool_new_with_policy:
 * @initial_capacity: Initial number of particles to allocate
 * @policy: The grow policy to use
 * @max_capacity: Maximum capacity (0 for unlimited)
 *
 * Creates a new particle pool with custom growth settings.
 *
 * Returns: (transfer full): A new #LrgParticlePool
 */
LrgParticlePool *
lrg_particle_pool_new_with_policy (guint             initial_capacity,
                                   LrgPoolGrowPolicy policy,
                                   guint             max_capacity)
{
    LrgParticlePool *self;

    if (initial_capacity == 0)
        initial_capacity = DEFAULT_INITIAL_CAPACITY;

    self = g_object_new (LRG_TYPE_PARTICLE_POOL, NULL);
    self->grow_policy = policy;
    self->max_capacity = max_capacity;

    lrg_particle_pool_grow (self, initial_capacity);

    return self;
}

/**
 * lrg_particle_pool_acquire:
 * @self: A #LrgParticlePool
 *
 * Acquires a particle from the pool.
 *
 * Returns: (transfer none) (nullable): A particle, or %NULL if full
 */
LrgParticle *
lrg_particle_pool_acquire (LrgParticlePool *self)
{
    LrgParticle *particle;
    guint        index;
    guint        new_capacity;

    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), NULL);

    /* Try to get from free list */
    if (self->free_count > 0)
    {
        self->free_count--;
        index = self->free_indices[self->free_count];
        particle = &self->particles[index];
        particle->alive = TRUE;
        self->alive_count++;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIVE_COUNT]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);

        return particle;
    }

    /* Pool is exhausted, try to grow based on policy */
    switch (self->grow_policy)
    {
    case LRG_POOL_GROW_NONE:
        return NULL;

    case LRG_POOL_GROW_LINEAR:
        new_capacity = self->capacity + DEFAULT_INITIAL_CAPACITY;
        break;

    case LRG_POOL_GROW_DOUBLE:
        new_capacity = self->capacity * 2;
        if (new_capacity < DEFAULT_INITIAL_CAPACITY)
            new_capacity = DEFAULT_INITIAL_CAPACITY;
        break;

    case LRG_POOL_GROW_RECYCLE:
        /* Find and recycle the oldest alive particle */
        {
            guint  oldest_index = 0;
            gfloat oldest_age = -1.0f;
            guint  i;

            for (i = 0; i < self->capacity; i++)
            {
                if (self->particles[i].alive &&
                    self->particles[i].age > oldest_age)
                {
                    oldest_age = self->particles[i].age;
                    oldest_index = i;
                }
            }

            if (oldest_age >= 0.0f)
            {
                particle = &self->particles[oldest_index];
                lrg_particle_reset (particle);
                particle->alive = TRUE;
                return particle;
            }
        }
        return NULL;

    default:
        return NULL;
    }

    if (!lrg_particle_pool_grow (self, new_capacity))
        return NULL;

    /* Retry acquisition after growth */
    return lrg_particle_pool_acquire (self);
}

/**
 * lrg_particle_pool_release:
 * @self: A #LrgParticlePool
 * @particle: The particle to release
 *
 * Releases a particle back to the pool.
 */
void
lrg_particle_pool_release (LrgParticlePool *self,
                           LrgParticle     *particle)
{
    guint index;

    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));
    g_return_if_fail (particle != NULL);

    if (!particle->alive)
        return;

    /* Calculate index from pointer */
    index = (guint) (particle - self->particles);
    g_return_if_fail (index < self->capacity);

    /* Reset and mark as dead */
    lrg_particle_reset (particle);

    /* Add to free list */
    self->free_indices[self->free_count++] = index;
    self->alive_count--;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIVE_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
}

/**
 * lrg_particle_pool_release_dead:
 * @self: A #LrgParticlePool
 *
 * Releases all dead particles back to the free list.
 *
 * Returns: The number of particles released
 */
guint
lrg_particle_pool_release_dead (LrgParticlePool *self)
{
    guint i;
    guint released;

    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    released = 0;

    for (i = 0; i < self->capacity; i++)
    {
        LrgParticle *p = &self->particles[i];

        /* Check if the particle just died (was alive, now dead due to lifetime) */
        if (!p->alive && self->alive_count > 0)
        {
            /*
             * This is a bit tricky - we need to check if this particle
             * is already in the free list or not. We'll scan the free
             * list to avoid duplicates.
             */
            gboolean in_free_list = FALSE;
            guint j;

            for (j = 0; j < self->free_count; j++)
            {
                if (self->free_indices[j] == i)
                {
                    in_free_list = TRUE;
                    break;
                }
            }

            if (!in_free_list)
            {
                self->free_indices[self->free_count++] = i;
                self->alive_count--;
                released++;
            }
        }
    }

    if (released > 0)
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIVE_COUNT]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
    }

    return released;
}

/**
 * lrg_particle_pool_get_capacity:
 * @self: A #LrgParticlePool
 *
 * Gets the total capacity of the pool.
 *
 * Returns: The pool capacity
 */
guint
lrg_particle_pool_get_capacity (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    return self->capacity;
}

/**
 * lrg_particle_pool_get_alive_count:
 * @self: A #LrgParticlePool
 *
 * Gets the number of currently alive particles.
 *
 * Returns: The number of alive particles
 */
guint
lrg_particle_pool_get_alive_count (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    return self->alive_count;
}

/**
 * lrg_particle_pool_get_free_count:
 * @self: A #LrgParticlePool
 *
 * Gets the number of free particles.
 *
 * Returns: The number of free particles
 */
guint
lrg_particle_pool_get_free_count (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    return self->free_count;
}

/**
 * lrg_particle_pool_is_full:
 * @self: A #LrgParticlePool
 *
 * Checks if the pool is full.
 *
 * Returns: %TRUE if the pool is full
 */
gboolean
lrg_particle_pool_is_full (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), TRUE);

    if (self->free_count > 0)
        return FALSE;

    if (self->grow_policy == LRG_POOL_GROW_NONE)
        return TRUE;

    if (self->max_capacity > 0 && self->capacity >= self->max_capacity)
        return TRUE;

    return FALSE;
}

/**
 * lrg_particle_pool_is_empty:
 * @self: A #LrgParticlePool
 *
 * Checks if the pool has no alive particles.
 *
 * Returns: %TRUE if no particles are alive
 */
gboolean
lrg_particle_pool_is_empty (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), TRUE);

    return self->alive_count == 0;
}

/**
 * lrg_particle_pool_clear:
 * @self: A #LrgParticlePool
 *
 * Kills and releases all particles.
 */
void
lrg_particle_pool_clear (LrgParticlePool *self)
{
    guint i;

    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));

    /* Reset all particles and rebuild free list */
    self->free_count = 0;

    for (i = 0; i < self->capacity; i++)
    {
        lrg_particle_reset (&self->particles[i]);
        self->free_indices[self->free_count++] = i;
    }

    self->alive_count = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIVE_COUNT]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
}

/**
 * lrg_particle_pool_foreach_alive:
 * @self: A #LrgParticlePool
 * @func: (scope call): Callback function
 * @user_data: (closure): User data
 *
 * Iterates over all alive particles.
 */
void
lrg_particle_pool_foreach_alive (LrgParticlePool        *self,
                                 LrgParticlePoolIterFunc func,
                                 gpointer                user_data)
{
    guint i;

    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));
    g_return_if_fail (func != NULL);

    for (i = 0; i < self->capacity; i++)
    {
        LrgParticle *p = &self->particles[i];

        if (p->alive)
        {
            if (!func (p, user_data))
                break;
        }
    }
}

/**
 * lrg_particle_pool_get_particles:
 * @self: A #LrgParticlePool
 * @out_count: (out) (optional): Location to store count
 *
 * Gets direct access to the particle array.
 *
 * Returns: (transfer none) (array length=out_count): The particle array
 */
LrgParticle *
lrg_particle_pool_get_particles (LrgParticlePool *self,
                                 guint           *out_count)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), NULL);

    if (out_count != NULL)
        *out_count = self->capacity;

    return self->particles;
}

/**
 * lrg_particle_pool_update_all:
 * @self: A #LrgParticlePool
 * @delta_time: Time step in seconds
 *
 * Updates all alive particles.
 *
 * Returns: The number of particles still alive
 */
guint
lrg_particle_pool_update_all (LrgParticlePool *self,
                              gfloat           delta_time)
{
    guint i;
    guint still_alive;

    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    still_alive = 0;

    for (i = 0; i < self->capacity; i++)
    {
        LrgParticle *p = &self->particles[i];

        if (p->alive)
        {
            if (lrg_particle_update (p, delta_time))
            {
                still_alive++;
            }
            else
            {
                /* Particle died, add to free list */
                self->free_indices[self->free_count++] = i;
            }
        }
    }

    if (still_alive != self->alive_count)
    {
        self->alive_count = still_alive;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ALIVE_COUNT]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
    }

    return still_alive;
}

/**
 * lrg_particle_pool_get_grow_policy:
 * @self: A #LrgParticlePool
 *
 * Gets the current grow policy.
 *
 * Returns: The grow policy
 */
LrgPoolGrowPolicy
lrg_particle_pool_get_grow_policy (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), LRG_POOL_GROW_NONE);

    return self->grow_policy;
}

/**
 * lrg_particle_pool_set_grow_policy:
 * @self: A #LrgParticlePool
 * @policy: The new grow policy
 *
 * Sets the grow policy.
 */
void
lrg_particle_pool_set_grow_policy (LrgParticlePool   *self,
                                   LrgPoolGrowPolicy  policy)
{
    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));

    if (self->grow_policy == policy)
        return;

    self->grow_policy = policy;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROW_POLICY]);
}

/**
 * lrg_particle_pool_get_max_capacity:
 * @self: A #LrgParticlePool
 *
 * Gets the maximum capacity.
 *
 * Returns: The maximum capacity (0 = unlimited)
 */
guint
lrg_particle_pool_get_max_capacity (LrgParticlePool *self)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), 0);

    return self->max_capacity;
}

/**
 * lrg_particle_pool_set_max_capacity:
 * @self: A #LrgParticlePool
 * @max_capacity: The new maximum capacity
 *
 * Sets the maximum capacity.
 */
void
lrg_particle_pool_set_max_capacity (LrgParticlePool *self,
                                    guint            max_capacity)
{
    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));

    if (self->max_capacity == max_capacity)
        return;

    self->max_capacity = max_capacity;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_CAPACITY]);
}

/**
 * lrg_particle_pool_reserve:
 * @self: A #LrgParticlePool
 * @capacity: The desired minimum capacity
 *
 * Ensures the pool has at least the specified capacity.
 *
 * Returns: %TRUE if successful
 */
gboolean
lrg_particle_pool_reserve (LrgParticlePool *self,
                           guint            capacity)
{
    g_return_val_if_fail (LRG_IS_PARTICLE_POOL (self), FALSE);

    if (self->capacity >= capacity)
        return TRUE;

    return lrg_particle_pool_grow (self, capacity);
}

/**
 * lrg_particle_pool_shrink_to_fit:
 * @self: A #LrgParticlePool
 *
 * Shrinks the pool to fit the current alive particles.
 */
void
lrg_particle_pool_shrink_to_fit (LrgParticlePool *self)
{
    LrgParticle *new_particles;
    guint       *new_free_indices;
    guint        new_capacity;
    guint        src_idx;
    guint        dst_idx;

    g_return_if_fail (LRG_IS_PARTICLE_POOL (self));

    if (self->alive_count == 0)
    {
        /* Clear everything */
        g_clear_pointer (&self->particles, g_free);
        g_clear_pointer (&self->free_indices, g_free);
        self->capacity = 0;
        self->free_count = 0;

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPACITY]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
        return;
    }

    /* Compact alive particles to the front */
    new_capacity = self->alive_count;
    new_particles = g_new (LrgParticle, new_capacity);

    dst_idx = 0;
    for (src_idx = 0; src_idx < self->capacity && dst_idx < new_capacity; src_idx++)
    {
        if (self->particles[src_idx].alive)
        {
            new_particles[dst_idx] = self->particles[src_idx];
            dst_idx++;
        }
    }

    /* Rebuild free list (empty since we're at capacity) */
    new_free_indices = g_new (guint, new_capacity);

    g_free (self->particles);
    g_free (self->free_indices);

    self->particles = new_particles;
    self->free_indices = new_free_indices;
    self->capacity = new_capacity;
    self->free_count = 0;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CAPACITY]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FREE_COUNT]);
}
