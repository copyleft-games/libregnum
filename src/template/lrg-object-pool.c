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
 * - Pre-warm during loading to avoid constructing gameplay objects on acquisition
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

/*
 * Each acquisition has a distinct identity, even when its object is reused.
 * active_index is the lease's slot in the pool's active arrays while it is
 * live (kept up to date by swap-remove fix-ups), and released turns TRUE
 * once the acquisition ends, so iteration snapshots holding the lease can
 * tell without a lookup.
 *
 * A lease owns one reference on its object for as long as the lease lives.
 * ref_count counts the pool's own reference (while active) plus one per
 * iteration snapshot holding it. The pool is single-threaded, so this is a
 * plain counter, and dead leases go on the pool's free list (linked through
 * next_free) instead of back to the allocator: steady-state acquire/release
 * allocates nothing.
 */
typedef struct _PoolLease PoolLease;

struct _PoolLease
{
    GObject   *object;
    guint      active_index;
    guint      ref_count;
    gboolean   released;
    PoolLease *next_free;
};

/*
 * One per pooled object, for the object's whole time in the pool. While the
 * object is available the member owns the pool's reference on it and lease
 * is NULL; while it is acquired that reference belongs to lease. The
 * available array holds members, so acquisition needs no lookup at all and
 * release needs a single one.
 */
typedef struct
{
    GObject   *object;
    PoolLease *lease;
} PoolMember;

struct _LrgObjectPool
{
    GObject parent_instance;

    GType               object_type;
    guint               initial_size;
    guint               max_size;
    LrgPoolGrowthPolicy growth_policy;

    /* Object storage */
    GPtrArray *available;   /* Owned PoolMember of inactive objects */
    GPtrArray *active_leases; /* Borrowed PoolLease of active objects */
    GHashTable *members;    /* Every pooled object -> its owned PoolMember.
                             * Keys only change when objects are created or
                             * dropped, never per acquisition, so the table
                             * is not touched (or resized) every frame. */
    GPtrArray *scratch;     /* Reusable foreach snapshot (NULL while in use) */
    PoolLease *free_leases; /* Recycled leases (singly linked) */

    /* The pooled type's class and LrgPoolable vtable, peeked once: every
     * pooled object is exactly object_type, so per-call interface lookups
     * are redundant. iface is NULL when object_type is not poolable. */
    gpointer              object_class;
    LrgPoolableInterface *iface;
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
 * Calls the pooled type's set_active through the cached vtable. Falls back
 * to the checked public wrapper when the type is not poolable, so misuse
 * still warns as before.
 */
static inline void
pool_set_active (LrgObjectPool *self,
                 GObject       *object,
                 gboolean       active)
{
    if (G_UNLIKELY (self->iface == NULL))
        lrg_poolable_set_active (LRG_POOLABLE (object), active);
    else if (self->iface->set_active != NULL)
        self->iface->set_active (LRG_POOLABLE (object), active);
}

/*
 * Calls the pooled type's reset through the cached vtable (see
 * pool_set_active()).
 */
static inline void
pool_reset (LrgObjectPool *self,
            GObject       *object)
{
    if (G_UNLIKELY (self->iface == NULL))
        lrg_poolable_reset (LRG_POOLABLE (object));
    else if (self->iface->reset != NULL)
        self->iface->reset (LRG_POOLABLE (object));
}

/*
 * Takes a lease from the free list (or allocates one) holding the pool's
 * reference and @object, whose reference the caller transfers to it.
 */
static PoolLease *
pool_lease_new (LrgObjectPool *self,
                GObject       *object)
{
    PoolLease *lease;

    lease = self->free_leases;
    if (lease != NULL)
        self->free_leases = lease->next_free;
    else
        lease = g_new (PoolLease, 1);

    lease->object = object;
    lease->active_index = 0;
    lease->ref_count = 1;
    lease->released = FALSE;
    lease->next_free = NULL;
    return lease;
}

/*
 * Drops one reference on @lease. The last one drops the lease's object
 * reference (if it still has one) and recycles the lease.
 */
static void
pool_lease_unref (LrgObjectPool *self,
                  PoolLease     *lease)
{
    if (--lease->ref_count > 0)
        return;

    g_clear_object (&lease->object);
    lease->next_free = self->free_leases;
    self->free_leases = lease;
}

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
    pool_set_active (self, object, FALSE);

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

    current_size = self->available->len + self->active_leases->len;

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

    current_size = self->available->len + self->active_leases->len;

    /* Respect max size */
    if (self->max_size > 0 && current_size + count > self->max_size)
        count = self->max_size - current_size;

    if (count == 0)
        return;

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pool growing by %u objects (type: %s)",
               count, g_type_name (self->object_type));

    for (i = 0; i < count; i++)
    {
        PoolMember *member = g_new (PoolMember, 1);

        member->object = pool_create_object (self);
        member->lease = NULL;
        g_ptr_array_add (self->available, member);
        g_hash_table_insert (self->members, member->object, member);
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

    /* Peek the vtable once; the class reference keeps it valid for the
     * pool's lifetime even while no pooled object exists. */
    self->object_class = g_type_class_ref (self->object_type);
    self->iface = g_type_interface_peek (self->object_class, LRG_TYPE_POOLABLE);

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
    g_clear_pointer (&self->active_leases, g_ptr_array_unref);
    g_clear_pointer (&self->members, g_hash_table_unref);
    g_clear_pointer (&self->scratch, g_ptr_array_unref);
    while (self->free_leases != NULL)
    {
        PoolLease *lease = self->free_leases;

        self->free_leases = lease->next_free;
        g_free (lease);
    }
    self->iface = NULL;
    g_clear_pointer (&self->object_class, g_type_class_unref);

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
        g_value_set_uint (value, self->active_leases->len);
        break;

    case PROP_AVAILABLE_COUNT:
        g_value_set_uint (value, self->available->len);
        break;

    case PROP_TOTAL_SIZE:
        g_value_set_uint (value, self->available->len + self->active_leases->len);
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
    self->available = g_ptr_array_new ();
    self->active_leases = g_ptr_array_new ();
    self->members = g_hash_table_new (g_direct_hash, g_direct_equal);
    self->scratch = g_ptr_array_new ();
    self->growth_policy = LRG_POOL_GROWTH_DOUBLE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

/*
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

/*
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

/*
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
    PoolMember *member;
    PoolLease *lease;

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
    /* Get from available and move to active; the member's reference moves
     * into the lease (no ref/unref round trip, no table update). */
    member = g_ptr_array_steal_index_fast (self->available,
                                            self->available->len - 1);
    lease = pool_lease_new (self, member->object);
    lease->active_index = self->active_leases->len;
    member->lease = lease;
    g_ptr_array_add (self->active_leases, lease);

    /* Mark as active */
    pool_set_active (self, member->object, TRUE);

    return member->object;
}

/*
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

/*
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
    PoolMember *member;
    PoolLease *lease;
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Find the acquisition: an object is active exactly while it has a
     * lease, and the lease knows its slot (no scan of the active array).
     * The lookup only hashes the pointer, so it is safe before the type
     * check; a hit is an object this pool created, which is poolable, so
     * the (interface, hence slow) instance check only runs on a miss. */
    member = g_hash_table_lookup (self->members, object);
    if (member == NULL || member->lease == NULL)
    {
        g_return_if_fail (LRG_IS_POOLABLE (object));
        g_warning ("LrgObjectPool: releasing object not from this pool");
        return;
    }

    /* Swap-remove from the active arrays, as before, and fix up the slot
     * of the lease that moved into the hole. */
    lease = member->lease;
    i = lease->active_index;
    g_ptr_array_remove_index_fast (self->active_leases, i);
    if (i < self->active_leases->len)
    {
        PoolLease *moved = g_ptr_array_index (self->active_leases, i);

        moved->active_index = i;
    }
    lease->released = TRUE;
    member->lease = NULL;

    /* Transfer pool ownership back to available storage. Snapshots retain the
     * old lease (and its object reference) until iteration finishes,
     * independently of future acquisitions; with no snapshot the lease's
     * reference simply moves back to the member. */
    if (lease->ref_count == 1)
        lease->object = NULL;
    else
        g_object_ref (object);
    pool_lease_unref (self, lease);

    /* Reset and deactivate */
    pool_reset (self, G_OBJECT (object));
    pool_set_active (self, G_OBJECT (object), FALSE);

    /* Add to available */
    g_ptr_array_add (self->available, member);
}

/*
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

    current_size = self->available->len + self->active_leases->len;
    needed = count > current_size ? count - current_size : 0;

    if (needed > 0)
        pool_grow (self, needed);
}

/*
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
    if (self->active_leases->len >= self->initial_size)
        target_available = 0;
    else
        target_available = self->initial_size - self->active_leases->len;

    /* Remove excess from available array */
    while (self->available->len > target_available)
    {
        PoolMember *member;

        member = g_ptr_array_steal_index_fast (self->available,
                                                self->available->len - 1);
        g_hash_table_remove (self->members, member->object);
        g_object_unref (member->object);
        g_free (member);
    }
}

/*
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
    GHashTableIter iter;
    gpointer member;
    g_autoptr(GPtrArray) active = NULL;
    g_autoptr(GPtrArray) available = NULL;
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Invalidate all acquisitions and forget every member first, so the pool
     * is already empty if dropping a reference below re-enters it. */
    for (i = 0; i < self->active_leases->len; i++)
    {
        PoolLease *lease = g_ptr_array_index (self->active_leases, i);

        lease->released = TRUE;
    }
    active = g_steal_pointer (&self->active_leases);
    available = g_steal_pointer (&self->available);
    self->active_leases = g_ptr_array_new ();
    self->available = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, self->members);
    while (g_hash_table_iter_next (&iter, NULL, &member))
    {
        /* Active members' references belong to their leases. */
        if (((PoolMember *) member)->lease != NULL)
        {
            g_hash_table_iter_remove (&iter);
            g_free (member);
        }
    }
    g_hash_table_remove_all (self->members);

    /* Drop the active leases' references, then the available objects'. */
    for (i = 0; i < active->len; i++)
        pool_lease_unref (self, g_ptr_array_index (active, i));
    for (i = 0; i < available->len; i++)
    {
        PoolMember *avail = g_ptr_array_index (available, i);

        g_object_unref (avail->object);
        g_free (avail);
    }
}

/* ==========================================================================
 * Pool Information
 * ========================================================================== */

/*
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

/*
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

    return self->active_leases->len;
}

/*
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

/*
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

    return self->available->len + self->active_leases->len;
}

/*
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

/*
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

/*
 * lrg_object_pool_foreach_active:
 * @self: an #LrgObjectPool
 * @callback: (scope call): callback for each active object
 * @user_data: user data for callback
 *
 * Iterates over a snapshot of active acquisitions in reverse storage order.
 * Returning %FALSE stops iteration early. Releasing any object, clearing the
 * pool, or starting a nested iteration from the callback is safe.
 *
 * Acquisitions released before their turn are skipped. New acquisitions wait
 * for a subsequent iteration, even if they reuse the same object. A nested
 * iteration takes its own snapshot. The pool and captured objects remain alive
 * until iteration ends, even when callbacks release their owners' references.
 * Storage order can change after releases; it is not acquisition order.
 *
 * Since: 1.0
 */
void
lrg_object_pool_foreach_active (LrgObjectPool            *self,
                                LrgObjectPoolForeachFunc  callback,
                                gpointer                  user_data)
{
    g_autoptr(LrgObjectPool) pool_ref = NULL;
    GPtrArray *snapshot;
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));
    g_return_if_fail (callback != NULL);

    pool_ref = g_object_ref (self);

    /* Reuse the pool's snapshot array; a nested iteration finds it taken
     * and makes its own. */
    snapshot = self->scratch != NULL ? g_steal_pointer (&self->scratch)
                                     : g_ptr_array_new ();
    /* Size the snapshot once, then fill it directly. */
    g_ptr_array_set_size (snapshot, self->active_leases->len);
    for (i = 0; i < self->active_leases->len; i++)
    {
        PoolLease *lease = g_ptr_array_index (self->active_leases,
                                              self->active_leases->len - 1 - i);

        lease->ref_count++;
        snapshot->pdata[i] = lease;
    }

    for (i = 0; i < snapshot->len; i++)
    {
        PoolLease *lease = g_ptr_array_index (snapshot, i);

        /* Released (or cleared) since the snapshot: skip. */
        if (lease->released)
            continue;
        if (!callback (LRG_POOLABLE (lease->object), user_data))
            break;
    }

    for (i = 0; i < snapshot->len; i++)
        pool_lease_unref (self, g_ptr_array_index (snapshot, i));
    g_ptr_array_set_size (snapshot, 0);
    if (self->scratch == NULL)
        self->scratch = snapshot;
    else
        g_ptr_array_unref (snapshot);
}

/*
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

    while (self->active_leases->len > 0)
    {
        PoolLease *lease = g_ptr_array_index (self->active_leases,
                                              self->active_leases->len - 1);
        lrg_object_pool_release (self, LRG_POOLABLE (lease->object));
    }
}
