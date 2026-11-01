/* lrg-object-pool.c - Generic object pool for efficient object reuse
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "config.h"

#include "lrg-object-pool.h"
#include <string.h>
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
 * A plain growable pointer array. GPtrArray's add and remove are out-of-line
 * calls (with a capacity check and a NULL-termination path) on every
 * acquisition and release; the pool's arrays are pushed, popped and
 * swap-removed inline instead.
 */
typedef struct
{
    gpointer *items;
    guint     len;
    guint     cap;
} PoolVec;

/*
 * One per pooled object, for the object's whole time in the pool; the member
 * owns the pool's reference on the object.
 *
 * slot is the member's index in the pool's members array (and, for types
 * implementing get_pool_slot() / set_pool_slot(), the value stored on the
 * object), so release finds the member by index and verifies it by identity.
 * active_index is its index in the active array while acquired.
 *
 * Each acquisition has a distinct identity, even when the object is reused:
 * acquired_at is the pool's acquisition count when it was acquired. An
 * iteration snapshot records the count at entry, so a captured member is
 * still the captured acquisition exactly while it is active and was acquired
 * before the snapshot; a release, a clear or a later re-acquisition all fail
 * that test without per-acquisition records or reference counts.
 *
 * next_dead links members whose pool reference must outlive the iteration
 * that is running (see pool_member_drop()).
 */
typedef struct _PoolMember PoolMember;

struct _PoolMember
{
    GObject    *object;
    guint64     acquired_at;
    guint       slot;
    guint       active_index;
    gboolean    active;
    PoolMember *next_dead;
};

struct _LrgObjectPool
{
    GObject parent_instance;

    GType               object_type;
    guint               initial_size;
    guint               max_size;
    LrgPoolGrowthPolicy growth_policy;

    /* Object storage: every array holds PoolMember pointers. members owns
     * them (indexed by PoolMember.slot); available and active borrow. */
    PoolVec     members;
    PoolVec     available;
    PoolVec     active;
    GHashTable *member_table; /* object -> member, only for types without
                               * get_pool_slot() / set_pool_slot(); keys only
                               * change when objects are created or dropped */
    guint64     acquire_count;
    PoolVec     scratch;      /* Reusable foreach snapshot */
    gboolean    scratch_busy; /* scratch taken by a running iteration */
    guint       iterating;    /* Depth of running foreach_active() calls */
    PoolMember *dead;         /* Members dropped while iterating */

    /* The pooled type's class and LrgPoolable vtable, peeked once: every
     * pooled object is exactly object_type, so per-call interface lookups
     * are redundant. iface is NULL when object_type is not poolable;
     * has_slots is TRUE when it implements both slot accessors. */
    gpointer              object_class;
    LrgPoolableInterface *iface;
    gboolean              has_slots;
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

/*
 * The pool type, recorded by class_init. The public check calls
 * lrg_object_pool_get_type() (an out-of-line once-check) on every acquire
 * and release; the type is final, so comparing against the recorded type
 * with G_TYPE_CHECK_INSTANCE_TYPE()'s inline exact-match path is the same
 * test. Before class_init no pool instance can exist, and a zero type
 * matches nothing. The macro keeps its name, so g_return_*_if_fail()
 * messages are unchanged.
 */
static GType object_pool_type;
#undef LRG_IS_OBJECT_POOL
#define LRG_IS_OBJECT_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), object_pool_type))

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

/* Makes room for at least @min_cap items. */
static void
pool_vec_reserve (PoolVec *vec,
                  guint    min_cap)
{
    guint cap;

    if (min_cap <= vec->cap)
        return;

    cap = vec->cap > 0 ? vec->cap : 16;
    while (cap < min_cap)
        cap = cap <= G_MAXUINT / 2 ? cap * 2 : min_cap;
    vec->items = g_renew (gpointer, vec->items, cap);
    vec->cap = cap;
}

/* Appends @item. */
static inline void
pool_vec_push (PoolVec  *vec,
               gpointer  item)
{
    if (G_UNLIKELY (vec->len == vec->cap))
        pool_vec_reserve (vec, vec->len + 1);
    vec->items[vec->len++] = item;
}

/* Frees the storage and empties @vec. */
static void
pool_vec_free (PoolVec *vec)
{
    g_clear_pointer (&vec->items, g_free);
    vec->len = 0;
    vec->cap = 0;
}

/* Moves @src's storage into @dst (which must be free) and empties @src. */
static void
pool_vec_steal (PoolVec *dst,
                PoolVec *src)
{
    *dst = *src;
    src->items = NULL;
    src->len = 0;
    src->cap = 0;
}

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
 * Records @member's slot (its index in the members array) on the object,
 * for types that keep one.
 */
static inline void
pool_member_set_slot (LrgObjectPool *self,
                      PoolMember    *member,
                      guint          slot)
{
    member->slot = slot;
    if (self->has_slots)
        self->iface->set_pool_slot (LRG_POOLABLE (member->object), slot);
}

/*
 * pool_find_member:
 *
 * The member of @object, or %NULL when @object is not (or no longer) one of
 * this pool's objects. Every pooled object is exactly object_type, so any
 * other instance misses without a lookup. Slot types read the slot stored
 * on the object and verify it against the members array (bounds and
 * identity: a foreign, stale or corrupted slot just misses); other types
 * look the pointer up in the table, which hashes the pointer only. Nothing
 * here dereferences anything but @object's class pointer, so a non-poolable
 * argument reaches the caller's type check unharmed.
 */
static inline PoolMember *
pool_find_member (LrgObjectPool *self,
                  gpointer       object)
{
    if (G_UNLIKELY (object == NULL ||
                    ((GTypeInstance *) object)->g_class == NULL ||
                    G_TYPE_FROM_INSTANCE (object) != self->object_type))
        return NULL;

    if (G_LIKELY (self->has_slots))
    {
        guint       slot = self->iface->get_pool_slot (LRG_POOLABLE (object));
        PoolMember *member;

        if (slot >= self->members.len)
            return NULL;
        member = self->members.items[slot];
        return member->object == object ? member : NULL;
    }

    return self->member_table != NULL ?
        g_hash_table_lookup (self->member_table, object) : NULL;
}

/*
 * pool_member_drop:
 *
 * Disposes of a member that has already left every array and the table:
 * drops its object reference and frees it. While an iteration runs, a
 * snapshot may still point at the member and the iteration promises its
 * object stays alive, so the member is parked on the dead list instead
 * and dropped when the outermost iteration ends.
 */
static void
pool_member_drop (LrgObjectPool *self,
                  PoolMember    *member)
{
    member->active = FALSE;
    if (self->iterating > 0)
    {
        member->next_dead = self->dead;
        self->dead = member;
        return;
    }

    g_object_unref (member->object);
    g_free (member);
}

/*
 * Drops the members parked while iterating. Takes the whole list first: an
 * object's last unref may re-enter the pool.
 */
static void
pool_flush_dead (LrgObjectPool *self)
{
    PoolMember *member;

    member = g_steal_pointer (&self->dead);
    while (member != NULL)
    {
        PoolMember *next = member->next_dead;

        g_object_unref (member->object);
        g_free (member);
        member = next;
    }
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

    current_size = self->available.len + self->active.len;

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

    current_size = self->available.len + self->active.len;

    /* Respect max size */
    if (self->max_size > 0 && current_size + count > self->max_size)
        count = self->max_size - current_size;

    if (count == 0)
        return;

    lrg_debug (LRG_LOG_DOMAIN_TEMPLATE, "Pool growing by %u objects (type: %s)",
               count, g_type_name (self->object_type));

    /* Every array can hold every member: pushes below (and later
     * acquisitions and releases) never reallocate. */
    pool_vec_reserve (&self->members, current_size + count);
    pool_vec_reserve (&self->available, current_size + count);
    pool_vec_reserve (&self->active, current_size + count);

    for (i = 0; i < count; i++)
    {
        PoolMember *member = g_new0 (PoolMember, 1);

        member->object = pool_create_object (self);
        pool_member_set_slot (self, member, self->members.len);
        pool_vec_push (&self->members, member);
        pool_vec_push (&self->available, member);
        if (self->member_table != NULL)
            g_hash_table_insert (self->member_table, member->object, member);
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

    /* Types that keep a slot are found by index; the others by table */
    self->has_slots = self->iface != NULL &&
                      self->iface->get_pool_slot != NULL &&
                      self->iface->set_pool_slot != NULL;
    if (!self->has_slots)
        self->member_table = g_hash_table_new (g_direct_hash, NULL);

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

    /* dispose cleared the pool; nothing iterates a finalizing pool */
    pool_flush_dead (self);
    pool_vec_free (&self->members);
    pool_vec_free (&self->available);
    pool_vec_free (&self->active);
    pool_vec_free (&self->scratch);
    g_clear_pointer (&self->member_table, g_hash_table_unref);
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
        g_value_set_uint (value, self->active.len);
        break;

    case PROP_AVAILABLE_COUNT:
        g_value_set_uint (value, self->available.len);
        break;

    case PROP_TOTAL_SIZE:
        g_value_set_uint (value, self->available.len + self->active.len);
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

    object_pool_type = G_TYPE_FROM_CLASS (klass);

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
    /* PoolVecs start empty (zeroed instance); member_table is created in
     * constructed() once the pooled type is known. */
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

    g_return_val_if_fail (LRG_IS_OBJECT_POOL (self), NULL);

    /* Try to get an available object */
    if (self->available.len == 0)
    {
        guint growth = pool_calculate_growth (self);
        if (growth > 0)
            pool_grow (self, growth);
    }

    if (self->available.len == 0)
    {
        lrg_debug (LRG_LOG_DOMAIN_TEMPLATE,
                   "Pool exhausted: %s (max: %u)",
                   g_type_name (self->object_type),
                   self->max_size);
        return NULL;
    }

    /* Pop from available and push to active: no allocation, no reference
     * change, no table update. A new acquisition stamp gives this use of the
     * object its own identity (see PoolMember). */
    member = self->available.items[--self->available.len];
    member->active = TRUE;
    member->acquired_at = self->acquire_count++;
    member->active_index = self->active.len;
    pool_vec_push (&self->active, member);

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
    PoolMember *moved;
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Find the acquisition: an object is active exactly while its member
     * is (pool_find_member() never trusts the argument beyond its class
     * pointer); the (interface, hence slow) instance check only runs on a
     * miss. */
    member = pool_find_member (self, object);
    if (G_UNLIKELY (member == NULL || !member->active))
    {
        g_return_if_fail (LRG_IS_POOLABLE (object));
        g_warning ("LrgObjectPool: releasing object not from this pool");
        return;
    }

    /* Swap-remove from the active array, as before, and fix up the slot
     * of the member that moved into the hole. */
    i = member->active_index;
    moved = self->active.items[--self->active.len];
    if (i < self->active.len)
    {
        self->active.items[i] = moved;
        moved->active_index = i;
    }
    member->active = FALSE;

    /* Reset and deactivate. The member keeps the pool's reference
     * throughout; a snapshot holding it sees it inactive and skips it.
     * The callbacks run as if inside an iteration, so one that clears or
     * shrinks the pool parks this member instead of freeing it under us. */
    self->iterating++;
    pool_reset (self, member->object);
    pool_set_active (self, member->object, FALSE);
    self->iterating--;

    /* Back to available (capacity was reserved when the member was made),
     * unless the callbacks dropped it from the pool. */
    if (G_LIKELY (member->slot < self->members.len &&
                  self->members.items[member->slot] == member))
        pool_vec_push (&self->available, member);
    if (G_UNLIKELY (self->dead != NULL) && self->iterating == 0)
        pool_flush_dead (self);
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

    current_size = self->available.len + self->active.len;
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
    if (self->active.len >= self->initial_size)
        target_available = 0;
    else
        target_available = self->initial_size - self->active.len;

    /* Remove excess from available array */
    while (self->available.len > target_available)
    {
        PoolMember *member;

        PoolMember *last;

        member = self->available.items[--self->available.len];

        /* Swap-remove from the members array, keeping slots dense */
        last = self->members.items[--self->members.len];
        if (member != last)
        {
            self->members.items[member->slot] = last;
            pool_member_set_slot (self, last, member->slot);
        }
        if (self->member_table != NULL)
            g_hash_table_remove (self->member_table, member->object);
        pool_member_drop (self, member);
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
    PoolVec members;
    guint i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));

    /* Forget every member first, so the pool is already empty if dropping
     * a reference below re-enters it. Marking members inactive ends every
     * acquisition (snapshots skip them). */
    pool_vec_steal (&members, &self->members);
    self->available.len = 0;
    self->active.len = 0;
    if (self->member_table != NULL)
        g_hash_table_remove_all (self->member_table);

    /* Drop the objects (parked until the outermost iteration ends when
     * one is running, so captured objects stay alive). */
    for (i = 0; i < members.len; i++)
        pool_member_drop (self, members.items[i]);
    pool_vec_free (&members);
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

    return self->active.len;
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

    return self->available.len;
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

    return self->available.len + self->active.len;
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
    PoolVec  own;
    PoolVec *snapshot;
    guint64  stamp;
    guint    i;

    g_return_if_fail (LRG_IS_OBJECT_POOL (self));
    g_return_if_fail (callback != NULL);

    pool_ref = g_object_ref (self);

    /* Capture the active members: a plain copy of the active array into the
     * pool's reusable buffer (a nested iteration finds it busy and uses its
     * own), walked backwards for reverse storage order. No reference counts
     * move per member: members are only freed through pool_member_drop(),
     * which parks them until the outermost iteration ends, so every
     * captured pointer stays valid; and the acquisition stamp tells a
     * captured acquisition from a release or a later re-acquisition. */
    own.items = NULL;
    own.len = 0;
    own.cap = 0;
    snapshot = self->scratch_busy ? &own : &self->scratch;
    if (snapshot == &self->scratch)
        self->scratch_busy = TRUE;
    pool_vec_reserve (snapshot, self->active.len);
    snapshot->len = self->active.len;
    if (snapshot->len > 0)
        memcpy (snapshot->items, self->active.items,
                snapshot->len * sizeof (gpointer));
    stamp = self->acquire_count;

    self->iterating++;
    for (i = snapshot->len; i > 0; i--)
    {
        PoolMember *member = snapshot->items[i - 1];

        /* Released, cleared or re-acquired since the snapshot: skip. */
        if (!member->active || member->acquired_at >= stamp)
            continue;
        if (!callback (LRG_POOLABLE (member->object), user_data))
            break;
    }
    self->iterating--;

    snapshot->len = 0;
    if (snapshot == &self->scratch)
        self->scratch_busy = FALSE;
    else
        pool_vec_free (&own);

    /* The outermost iteration drops what was parked while it ran */
    if (self->iterating == 0)
        pool_flush_dead (self);
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

    while (self->active.len > 0)
    {
        PoolMember *member = self->active.items[self->active.len - 1];

        lrg_object_pool_release (self, LRG_POOLABLE (member->object));
    }
}
