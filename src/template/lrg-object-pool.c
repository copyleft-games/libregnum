/* lrg-object-pool.c - Generic object pool for efficient object reuse
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-object-pool.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-object-pool
 * @title: LrgObjectPool
 * @short_description: Generic object pool for efficient object reuse
 * @see_also: #LrgPoolable
 *
 * #LrgObjectPool provides a generic object pooling mechanism that reduces
 * allocation overhead by reusing objects. This is particularly useful for
 * frequently created/destroyed objects like bullets, particles, and enemies.
 *
 * ## Performance Considerations
 *
 * - Pre-warm the pool during loading to avoid allocation during gameplay
 * - Use LRG_POOL_GROWTH_FIXED with sufficient initial size for predictable memory
 * - Call shrink_to_fit() during level transitions to reduce memory usage
 *
 * ## Thread Safety
 *
 * LrgObjectPool is NOT thread-safe. All operations on a pool should be
 * performed from the same thread (typically the game thread).
 *
 * Since: 1.0
 */

struct _LrgObjectPool
{
    GObject parent_instance;

    GType               object_type;
    guint               initial_size;
    guint               max_size;
    LrgPoolGrowthPolicy growth_policy;

    /* Object storage */
    GPtrArray *available;   /* Inactive objects ready for reuse */
    GPtrArray *active;      /* Currently in-use objects */
};

enum
{
    PROP_0,
    PROP_OBJECT_TYPE,
    PROP_INITIAL_SIZE,
    PROP_MAX_SIZE,
    PROP_GROWTH_POLICY,
    PROP_ACTIVE_COUNT,
    PROP_AVAILABLE_COUNT,
    PROP_TOTAL_SIZE,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_FINAL_TYPE (LrgObjectPool, lrg_object_pool, G_TYPE_OBJECT)

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/*
 * Creates a new object for the pool.
 */
static GObject *
pool_create_object (LrgObjectPool *self)
{
    GObject *object;

    object = g_object_new (self->object_type, NULL);

    /* Set the pool back-reference if the object supports it */
    /* Note: This is done via the set_active callback pattern */
    lrg_poolable_set_active (LRG_POOLABLE (object), FALSE);

    return object;
}

/*
 * Calculates how many objects to grow by.
 */
static guint
pool_calculate_growth (LrgObjectPool *self)
{
    guint current_size;
    guint growth;

    current_size = self->available->len + self->active->len;

    switch (self->growth_policy)
    {
    case LRG_POOL_GROWTH_FIXED:
        /* Never grow */
        return 0;

    case LRG_POOL_GROWTH_LINEAR:
        /* Grow by initial size */
        growth = self->initial_size > 0 ? self->initial_size : 16;
        break;

    case LRG_POOL_GROWTH_DOUBLE:
        /* Double the capacity */
        growth = current_size > 0 ? current_size : 16;
        break;

    case LRG_POOL_GROWTH_EXPONENTIAL:
        /* 1.5x the capacity */
        growth = current_size > 0 ? (current_size + 1) / 2 : 16;
        break;

    default:
        growth = 16;
        break;
    }

    /* Respect max size */
    if (self->max_size > 0)
    {
        guint available = self->max_size - current_size;
        if (growth > available)
            growth = available;
    }

    return growth;
}

/*
 * Grows the pool by allocating new objects.
 */
static void
pool_grow (LrgObjectPool *self,
           guint          count)
{
    guint i;
    guint current_size;

    current_size = self->available->len + self->active->len;

    /* Respect max size */
    if (self->max_size > 0 && current_size + count > self->max_size)
        count = self->max_size - current_size;

    if (count == 0)
        return;

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pool growing by %u objects (type: %s)",
               count, g_type_name (self->object_type));

    for (i = 0; i < count; i++)
    {
        GObject *object = pool_create_object (self);
        g_ptr_array_add (self->available, object);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_object_pool_constructed (GObject *object)
{
    LrgObjectPool *self = LRG_OBJECT_POOL (object);

    G_OBJECT_CLASS (lrg_object_pool_parent_class)->constructed (object);

    /* Validate object type implements LrgPoolable */
    if (!g_type_is_a (self->object_type, LRG_TYPE_POOLABLE))
    {
        g_critical ("LrgObjectPool: type %s does not implement LrgPoolable",
                    g_type_name (self->object_type));
        return;
    }

    /* Pre-allocate initial objects */
    if (self->initial_size > 0)
        pool_grow (self, self->initial_size);
}

static void
lrg_object_pool_dispose (GObject *object)
{
    LrgObjectPool *self = LRG_OBJECT_POOL (object);

    lrg_object_pool_clear (self);

    G_OBJECT_CLASS (lrg_object_pool_parent_class)->dispose (object);
}

static void
lrg_object_pool_finalize (GObject *object)
{
    LrgObjectPool *self = LRG_OBJECT_POOL (object);

    g_clear_pointer (&self->available, g_ptr_array_unref);
    g_clear_pointer (&self->active, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_object_pool_parent_class)->finalize (object);
}

static void
lrg_object_pool_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgObjectPool *self = LRG_OBJECT_POOL (object);

    switch (prop_id)
    {
    case PROP_OBJECT_TYPE:
        self->object_type = g_value_get_gtype (value);
        break;

    case PROP_INITIAL_SIZE:
        self->initial_size = g_value_get_uint (value);
        break;

    case PROP_MAX_SIZE:
        self->max_size = g_value_get_uint (value);
        break;

    case PROP_GROWTH_POLICY:
        self->growth_policy = g_value_get_enum (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_object_pool_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgObjectPool *self = LRG_OBJECT_POOL (object);

    switch (prop_id)
    {
    case PROP_OBJECT_TYPE:
        g_value_set_gtype (value, self->object_type);
        break;

    case PROP_INITIAL_SIZE:
        g_value_set_uint (value, self->initial_size);
        break;

    case PROP_MAX_SIZE:
        g_value_set_uint (value, self->max_size);
        break;

    case PROP_GROWTH_POLICY:
        g_value_set_enum (value, self->growth_policy);
        break;

    case PROP_ACTIVE_COUNT:
        g_value_set_uint (value, self->active->len);
        break;

    case PROP_AVAILABLE_COUNT:
        g_value_set_uint (value, self->available->len);
        break;

    case PROP_TOTAL_SIZE:
        g_value_set_uint (value, self->available->len + self->active->len);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_object_pool_class_init (LrgObjectPoolClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = lrg_object_pool_constructed;
    object_class->dispose = lrg_object_pool_dispose;
    object_class->finalize = lrg_object_pool_finalize;
    object_class->set_property = lrg_object_pool_set_property;
    object_class->get_property = lrg_object_pool_get_property;

    /**
     * LrgObjectPool:object-type:
     *
     * The GType of objects managed by this pool. The type must
     * implement the #LrgPoolable interface.
     *
     * Since: 1.0
     */
    properties[PROP_OBJECT_TYPE] =
        g_param_spec_gtype ("object-type",
                            "Object Type",
                            "GType of pooled objects",
                            G_TYPE_OBJECT,
                            G_PARAM_READWRITE |
                            G_PARAM_CONSTRUCT_ONLY |
                            G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:initial-size:
     *
     * Initial pool capacity. This many objects are pre-allocated
     * when the pool is created.
     *
     * Since: 1.0
     */
    properties[PROP_INITIAL_SIZE] =
        g_param_spec_uint ("initial-size",
                           "Initial Size",
                           "Initial pool capacity",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:max-size:
     *
     * Maximum pool size. 0 means unlimited.
     *
     * Since: 1.0
     */
    properties[PROP_MAX_SIZE] =
        g_param_spec_uint ("max-size",
                           "Max Size",
                           "Maximum pool size (0 = unlimited)",
                           0, G_MAXUINT, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:growth-policy:
     *
     * How the pool should grow when exhausted.
     *
     * Since: 1.0
     */
    properties[PROP_GROWTH_POLICY] =
        g_param_spec_enum ("growth-policy",
                           "Growth Policy",
                           "Pool growth policy",
                           LRG_TYPE_POOL_GROWTH_POLICY,
                           LRG_POOL_GROWTH_DOUBLE,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT_ONLY |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:active-count:
     *
     * Number of currently active (in-use) objects.
     *
     * Since: 1.0
     */
    properties[PROP_ACTIVE_COUNT] =
        g_param_spec_uint ("active-count",
                           "Active Count",
                           "Number of active objects",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:available-count:
     *
     * Number of available (inactive) objects.
     *
     * Since: 1.0
     */
    properties[PROP_AVAILABLE_COUNT] =
        g_param_spec_uint ("available-count",
                           "Available Count",
                           "Number of available objects",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    /**
     * LrgObjectPool:total-size:
     *
     * Total number of objects (active + available).
     *
     * Since: 1.0
     */
    properties[PROP_TOTAL_SIZE] =
        g_param_spec_uint ("total-size",
                           "Total Size",
                           "Total number of objects",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_object_pool_init (LrgObjectPool *self)
{
    self->available = g_ptr_array_new_with_free_func (g_object_unref);
    self->active = g_ptr_array_new ();
    self->growth_policy = LRG_POOL_GROWTH_DOUBLE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_object_pool_new:
 * @object_type: the #GType of objects to pool
 * @initial_size: initial pool capacity
 * @growth_policy: how the pool should grow
 *
 * Creates a new object pool.
 *
 * Returns: (transfer full): a new #LrgObjectPool
 *
 * Since: 1.0
 */
LrgObjectPool *
lrg_object_pool_new (GType               object_type,
                     guint               initial_size,
                     LrgPoolGrowthPolicy growth_policy)
{
    return g_object_new (LRG_TYPE_OBJECT_POOL,
                         "object-type", object_type,
                         "initial-size", initial_size,
                         "growth-policy", growth_policy,
                         NULL);
}

/**
 * lrg_object_pool_new_with_max:
 * @object_type: the #GType of objects to pool
 * @initial_size: initial pool capacity
 * @max_size: maximum pool size (0 = unlimited)
 * @growth_policy: how the pool should grow
 *
 * Creates a new object pool with a maximum size.
 *
 * Returns: (transfer full): a new #LrgObjectPool
 *
 * Since: 1.0
 */
LrgObjectPool *
lrg_object_pool_new_with_max (GType               object_type,
                              guint               initial_size,
                              guint               max_size,
                              LrgPoolGrowthPolicy growth_policy)
{
    return g_object_new (LRG_TYPE_OBJECT_POOL,
                         "object-type", object_type,
                         "initial-size", initial_size,
                         "max-size", max_size,
                         "growth-policy", growth_policy,
                         NULL);
}

/* ==========================================================================
 * Pool Operations
 * ========================================================================== */

/**
 * lrg_object_pool_acquire:
 * @self: an #LrgObjectPool
 *
 * Acquires an object from the pool.
 *
 * Returns: (transfer none) (nullable): a pooled object
 *
 * Since: 1.0
 */
GObject *
lrg_object_pool_acquire (LrgObjectPool *self)
{
    GObject *object;

    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), NULL);

    /* Try to get an available object */
    if (self->available->len == 0)
    {
        guint growth = pool_calculate_growth (self);
        if (growth > 0)
            pool_grow (self, growth);
    }

    if (self->available->len == 0)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "Pool exhausted: %s (max: %u)",
                   g_type_name (self->object_type),
                   self->max_size);
        return NULL;
    }

    /* Get from available and move to active */
    object = g_ptr_array_steal_index_fast (self->available,
                                            self->available->len - 1);
    g_ptr_array_add (self->active, object);

    /* Mark as active */
    lrg_poolable_set_active (LRG_POOLABLE (object), TRUE);

    return object;
}

/**
 * lrg_object_pool_acquire_with_init:
 * @self: an #LrgObjectPool
 * @first_property_name: (nullable): first property name
 * @...: property value pairs, NULL-terminated
 *
 * Acquires an object and sets properties.
 *
 * Returns: (transfer none) (nullable): a pooled object
 *
 * Since: 1.0
 */
GObject *
lrg_object_pool_acquire_with_init (LrgObjectPool *self,
                                    const gchar   *first_property_name,
                                    ...)
{
    GObject *object;
    va_list args;

    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), NULL);

    object = lrg_object_pool_acquire (self);
    if (object == NULL)
        return NULL;

    if (first_property_name != NULL)
    {
        va_start (args, first_property_name);
        g_object_set_valist (object, first_property_name, args);
        va_end (args);
    }

    return object;
}

/**
 * lrg_object_pool_release:
 * @self: an #LrgObjectPool
 * @object: (transfer none): the object to release
 *
 * Releases an object back to the pool.
 *
 * Since: 1.0
 */
void
lrg_object_pool_release (LrgObjectPool *self,
                         LrgPoolable   *object)
{
    guint i;
    gboolean found;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));
    g_return_if_fail (LRG_IS_POOLABLE (object));

    /* Find and remove from active array */
    found = FALSE;
    for (i = 0; i < self->active->len; i++)
    {
        if (g_ptr_array_index (self->active, i) == object)
        {
            g_ptr_array_remove_index_fast (self->active, i);
            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        g_warning ("LrgObjectPool: releasing object not from this pool");
        return;
    }

    /* Reset and deactivate */
    lrg_poolable_reset (object);
    lrg_poolable_set_active (object, FALSE);

    /* Add to available */
    g_ptr_array_add (self->available, object);
}

/**
 * lrg_object_pool_prewarm:
 * @self: an #LrgObjectPool
 * @count: number of objects to pre-allocate
 *
 * Pre-allocates objects in the pool.
 *
 * Since: 1.0
 */
void
lrg_object_pool_prewarm (LrgObjectPool *self,
                         guint          count)
{
    guint current_size;
    guint needed;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    current_size = self->available->len + self->active->len;
    needed = count > current_size ? count - current_size : 0;

    if (needed > 0)
        pool_grow (self, needed);
}

/**
 * lrg_object_pool_shrink_to_fit:
 * @self: an #LrgObjectPool
 *
 * Releases excess capacity.
 *
 * Since: 1.0
 */
void
lrg_object_pool_shrink_to_fit (LrgObjectPool *self)
{
    guint target_available;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Keep at least initial_size - active_count available */
    if (self->active->len >= self->initial_size)
        target_available = 0;
    else
        target_available = self->initial_size - self->active->len;

    /* Remove excess from available array */
    while (self->available->len > target_available)
    {
        g_ptr_array_remove_index (self->available, self->available->len - 1);
    }
}

/**
 * lrg_object_pool_clear:
 * @self: an #LrgObjectPool
 *
 * Releases all objects in the pool.
 *
 * Since: 1.0
 */
void
lrg_object_pool_clear (LrgObjectPool *self)
{
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Unref active objects (not managed by array free func) */
    for (i = 0; i < self->active->len; i++)
    {
        GObject *obj = g_ptr_array_index (self->active, i);
        g_object_unref (obj);
    }
    g_ptr_array_set_size (self->active, 0);

    /* Available array handles its own unrefs via free func */
    g_ptr_array_set_size (self->available, 0);
}

/* ==========================================================================
 * Pool Information
 * ========================================================================== */

/**
 * lrg_object_pool_get_object_type:
 * @self: an #LrgObjectPool
 *
 * Gets the GType of pooled objects.
 *
 * Returns: the #GType
 *
 * Since: 1.0
 */
GType
lrg_object_pool_get_object_type (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), G_TYPE_INVALID);

    return self->object_type;
}

/**
 * lrg_object_pool_get_active_count:
 * @self: an #LrgObjectPool
 *
 * Gets the active object count.
 *
 * Returns: number of active objects
 *
 * Since: 1.0
 */
guint
lrg_object_pool_get_active_count (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), 0);

    return self->active->len;
}

/**
 * lrg_object_pool_get_available_count:
 * @self: an #LrgObjectPool
 *
 * Gets the available object count.
 *
 * Returns: number of available objects
 *
 * Since: 1.0
 */
guint
lrg_object_pool_get_available_count (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), 0);

    return self->available->len;
}

/**
 * lrg_object_pool_get_total_size:
 * @self: an #LrgObjectPool
 *
 * Gets the total object count.
 *
 * Returns: total objects
 *
 * Since: 1.0
 */
guint
lrg_object_pool_get_total_size (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), 0);

    return self->available->len + self->active->len;
}

/**
 * lrg_object_pool_get_max_size:
 * @self: an #LrgObjectPool
 *
 * Gets the maximum pool size.
 *
 * Returns: max size (0 = unlimited)
 *
 * Since: 1.0
 */
guint
lrg_object_pool_get_max_size (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), 0);

    return self->max_size;
}

/**
 * lrg_object_pool_get_growth_policy:
 * @self: an #LrgObjectPool
 *
 * Gets the growth policy.
 *
 * Returns: the #LrgPoolGrowthPolicy
 *
 * Since: 1.0
 */
LrgPoolGrowthPolicy
lrg_object_pool_get_growth_policy (LrgObjectPool *self)
{
    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), LRG_POOL_GROWTH_DOUBLE);

    return self->growth_policy;
}

/* ==========================================================================
 * Iteration
 * ========================================================================== */

/**
 * lrg_object_pool_foreach_active:
 * @self: an #LrgObjectPool
 * @callback: (scope call): callback for each active object
 * @user_data: user data for callback
 *
 * Iterates over active objects.
 *
 * Since: 1.0
 */
void
lrg_object_pool_foreach_active (LrgObjectPool            *self,
                                LrgObjectPoolForeachFunc  callback,
                                gpointer                  user_data)
{
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));
    g_return_if_fail (callback != NULL);

    /* Iterate in reverse to allow safe removal during iteration */
    for (i = self->active->len; i > 0; i--)
    {
        LrgPoolable *obj = g_ptr_array_index (self->active, i - 1);
        if (!callback (obj, user_data))
            break;
    }
}

/**
 * lrg_object_pool_release_all_active:
 * @self: an #LrgObjectPool
 *
 * Releases all active objects.
 *
 * Since: 1.0
 */
void
lrg_object_pool_release_all_active (LrgObjectPool *self)
{
    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    while (self->active->len > 0)
    {
        LrgPoolable *obj = g_ptr_array_index (self->active, self->active->len - 1);
        lrg_object_pool_release (self, obj);
    }
}
