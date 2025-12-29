/* lrg-resource-pool.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-resource-pool.h"
#include "../lrg-log.h"

/* Entry in the resource pool */
typedef struct
{
    LrgResource *resource;
    gdouble      amount;
    gdouble      multiplier;
} ResourceEntry;

static void
resource_entry_free (gpointer data)
{
    ResourceEntry *entry = (ResourceEntry *)data;
    g_clear_object (&entry->resource);
    g_free (entry);
}

struct _LrgResourcePool
{
    GObject      parent_instance;

    GHashTable  *resources;         /* gchar* (id) -> ResourceEntry* */
    gdouble      global_multiplier;
};

G_DEFINE_TYPE (LrgResourcePool, lrg_resource_pool, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_GLOBAL_MULTIPLIER,
    N_PROPS
};

enum
{
    SIGNAL_RESOURCE_CHANGED,
    SIGNAL_RESOURCE_DEPLETED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_resource_pool_finalize (GObject *object)
{
    LrgResourcePool *self = LRG_RESOURCE_POOL (object);

    g_clear_pointer (&self->resources, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_resource_pool_parent_class)->finalize (object);
}

static void
lrg_resource_pool_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgResourcePool *self = LRG_RESOURCE_POOL (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_MULTIPLIER:
        g_value_set_double (value, self->global_multiplier);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_resource_pool_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgResourcePool *self = LRG_RESOURCE_POOL (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_MULTIPLIER:
        lrg_resource_pool_set_global_multiplier (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_resource_pool_class_init (LrgResourcePoolClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_resource_pool_finalize;
    object_class->get_property = lrg_resource_pool_get_property;
    object_class->set_property = lrg_resource_pool_set_property;

    /**
     * LrgResourcePool:global-multiplier:
     *
     * Global multiplier applied to all resource additions.
     */
    properties[PROP_GLOBAL_MULTIPLIER] =
        g_param_spec_double ("global-multiplier",
                             "Global Multiplier",
                             "Global multiplier for all additions",
                             0.0, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgResourcePool::resource-changed:
     * @self: the pool
     * @resource: the #LrgResource that changed
     * @old_amount: the previous amount
     * @new_amount: the new amount
     *
     * Emitted when a resource amount changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_RESOURCE_CHANGED] =
        g_signal_new ("resource-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 3,
                      LRG_TYPE_RESOURCE,
                      G_TYPE_DOUBLE,
                      G_TYPE_DOUBLE);

    /**
     * LrgResourcePool::resource-depleted:
     * @self: the pool
     * @resource: the #LrgResource that was depleted
     *
     * Emitted when a resource reaches its minimum value (usually 0).
     *
     * Since: 1.0
     */
    signals[SIGNAL_RESOURCE_DEPLETED] =
        g_signal_new ("resource-depleted",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_RESOURCE);
}

static void
lrg_resource_pool_init (LrgResourcePool *self)
{
    self->resources = g_hash_table_new_full (g_str_hash,
                                              g_str_equal,
                                              g_free,
                                              resource_entry_free);
    self->global_multiplier = 1.0;
}

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static ResourceEntry *
get_or_create_entry (LrgResourcePool *self,
                     LrgResource     *resource)
{
    const gchar *id;
    ResourceEntry *entry;

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->resources, id);

    if (entry == NULL)
    {
        entry = g_new0 (ResourceEntry, 1);
        entry->resource = g_object_ref (resource);
        entry->amount = 0.0;
        entry->multiplier = 1.0;
        g_hash_table_insert (self->resources, g_strdup (id), entry);
    }

    return entry;
}

static void
emit_changed_and_check_depleted (LrgResourcePool *self,
                                 LrgResource     *resource,
                                 gdouble          old_amount,
                                 gdouble          new_amount)
{
    gdouble min_value;

    g_signal_emit (self, signals[SIGNAL_RESOURCE_CHANGED], 0,
                   resource, old_amount, new_amount);

    /* Check if resource was depleted (reached minimum) */
    min_value = lrg_resource_get_min_value (resource);
    if (new_amount <= min_value && old_amount > min_value)
    {
        g_signal_emit (self, signals[SIGNAL_RESOURCE_DEPLETED], 0, resource);
    }
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgResourcePool *
lrg_resource_pool_new (void)
{
    return g_object_new (LRG_TYPE_RESOURCE_POOL, NULL);
}

/* ==========================================================================
 * Resource Operations
 * ========================================================================== */

gdouble
lrg_resource_pool_get (LrgResourcePool *self,
                       LrgResource     *resource)
{
    ResourceEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->resources, id);

    if (entry != NULL)
        return entry->amount;

    return 0.0;
}

gdouble
lrg_resource_pool_get_by_id (LrgResourcePool *self,
                             const gchar     *resource_id)
{
    ResourceEntry *entry;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0.0);
    g_return_val_if_fail (resource_id != NULL, 0.0);

    entry = g_hash_table_lookup (self->resources, resource_id);

    if (entry != NULL)
        return entry->amount;

    return 0.0;
}

void
lrg_resource_pool_set (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount)
{
    ResourceEntry *entry;
    gdouble old_amount;
    gdouble clamped;

    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));

    entry = get_or_create_entry (self, resource);
    old_amount = entry->amount;
    clamped = lrg_resource_clamp_amount (resource, amount);

    if (old_amount != clamped)
    {
        entry->amount = clamped;
        emit_changed_and_check_depleted (self, resource, old_amount, clamped);
    }
}

gdouble
lrg_resource_pool_add (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount)
{
    ResourceEntry *entry;
    gdouble old_amount;
    gdouble new_amount;
    gdouble effective_multiplier;
    gdouble actual_added;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);
    g_return_val_if_fail (amount >= 0.0, 0.0);

    if (amount == 0.0)
        return 0.0;

    entry = get_or_create_entry (self, resource);
    old_amount = entry->amount;

    /* Apply multipliers */
    effective_multiplier = self->global_multiplier * entry->multiplier;
    new_amount = old_amount + (amount * effective_multiplier);

    /* Clamp to resource limits */
    new_amount = lrg_resource_clamp_amount (resource, new_amount);

    if (old_amount != new_amount)
    {
        entry->amount = new_amount;
        actual_added = new_amount - old_amount;
        emit_changed_and_check_depleted (self, resource, old_amount, new_amount);
        return actual_added;
    }

    return 0.0;
}

gboolean
lrg_resource_pool_remove (LrgResourcePool *self,
                          LrgResource     *resource,
                          gdouble          amount)
{
    ResourceEntry *entry;
    gdouble old_amount;
    gdouble new_amount;
    gdouble min_value;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);
    g_return_val_if_fail (amount >= 0.0, FALSE);

    if (amount == 0.0)
        return TRUE;

    entry = get_or_create_entry (self, resource);
    old_amount = entry->amount;
    min_value = lrg_resource_get_min_value (resource);
    new_amount = old_amount - amount;

    /* Check if we have enough */
    if (new_amount < min_value)
        return FALSE;

    entry->amount = new_amount;
    emit_changed_and_check_depleted (self, resource, old_amount, new_amount);

    return TRUE;
}

gdouble
lrg_resource_pool_remove_clamped (LrgResourcePool *self,
                                  LrgResource     *resource,
                                  gdouble          amount)
{
    ResourceEntry *entry;
    gdouble old_amount;
    gdouble new_amount;
    gdouble min_value;
    gdouble actual_removed;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);
    g_return_val_if_fail (amount >= 0.0, 0.0);

    if (amount == 0.0)
        return 0.0;

    entry = get_or_create_entry (self, resource);
    old_amount = entry->amount;
    min_value = lrg_resource_get_min_value (resource);
    new_amount = old_amount - amount;

    /* Clamp to minimum */
    if (new_amount < min_value)
        new_amount = min_value;

    if (old_amount != new_amount)
    {
        entry->amount = new_amount;
        actual_removed = old_amount - new_amount;
        emit_changed_and_check_depleted (self, resource, old_amount, new_amount);
        return actual_removed;
    }

    return 0.0;
}

gboolean
lrg_resource_pool_has (LrgResourcePool *self,
                       LrgResource     *resource,
                       gdouble          amount)
{
    gdouble current;
    gdouble min_value;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    current = lrg_resource_pool_get (self, resource);
    min_value = lrg_resource_get_min_value (resource);

    /* Check if we can remove this amount and stay at or above min */
    return (current - amount) >= min_value;
}

gboolean
lrg_resource_pool_transfer (LrgResourcePool *self,
                            LrgResourcePool *destination,
                            LrgResource     *resource,
                            gdouble          amount)
{
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (destination), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);
    g_return_val_if_fail (amount >= 0.0, FALSE);

    if (amount == 0.0)
        return TRUE;

    /* Check if source has enough */
    if (!lrg_resource_pool_has (self, resource, amount))
        return FALSE;

    /* Perform transfer (note: add() applies multipliers, remove() does not) */
    lrg_resource_pool_remove (self, resource, amount);

    /*
     * For transfers, we bypass multipliers on the destination since
     * the amount is already "earned". We set directly instead.
     */
    {
        gdouble current = lrg_resource_pool_get (destination, resource);
        lrg_resource_pool_set (destination, resource, current + amount);
    }

    return TRUE;
}

gdouble
lrg_resource_pool_transfer_all (LrgResourcePool *self,
                                LrgResourcePool *destination,
                                LrgResource     *resource)
{
    gdouble amount;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (destination), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    amount = lrg_resource_pool_get (self, resource);

    if (amount > 0.0)
    {
        lrg_resource_pool_transfer (self, destination, resource, amount);
    }

    return amount;
}

void
lrg_resource_pool_clear (LrgResourcePool *self)
{
    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));

    g_hash_table_remove_all (self->resources);
}

void
lrg_resource_pool_clear_resource (LrgResourcePool *self,
                                  LrgResource     *resource)
{
    const gchar *id;
    ResourceEntry *entry;
    gdouble old_amount;

    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->resources, id);

    if (entry != NULL)
    {
        old_amount = entry->amount;
        g_hash_table_remove (self->resources, id);

        if (old_amount > 0.0)
        {
            emit_changed_and_check_depleted (self, resource, old_amount, 0.0);
        }
    }
}

/* ==========================================================================
 * Query Operations
 * ========================================================================== */

gboolean
lrg_resource_pool_is_empty (LrgResourcePool *self)
{
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), TRUE);

    return g_hash_table_size (self->resources) == 0;
}

gboolean
lrg_resource_pool_contains (LrgResourcePool *self,
                            LrgResource     *resource)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    id = lrg_resource_get_id (resource);
    return g_hash_table_contains (self->resources, id);
}

GList *
lrg_resource_pool_get_resources (LrgResourcePool *self)
{
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), NULL);

    g_hash_table_iter_init (&iter, self->resources);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        ResourceEntry *entry = (ResourceEntry *)value;
        result = g_list_prepend (result, entry->resource);
    }

    return result;
}

guint
lrg_resource_pool_get_count (LrgResourcePool *self)
{
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 0);

    return g_hash_table_size (self->resources);
}

typedef struct
{
    GFunc    func;
    gpointer user_data;
} ForeachData;

static void
foreach_wrapper (gpointer key G_GNUC_UNUSED,
                 gpointer value,
                 gpointer user_data)
{
    ForeachData *data = (ForeachData *)user_data;
    ResourceEntry *entry = (ResourceEntry *)value;
    data->func (entry->resource, data->user_data);
}

void
lrg_resource_pool_foreach (LrgResourcePool *self,
                           GFunc            func,
                           gpointer         user_data)
{
    ForeachData data;

    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));
    g_return_if_fail (func != NULL);

    data.func = func;
    data.user_data = user_data;

    g_hash_table_foreach (self->resources, foreach_wrapper, &data);
}

/* ==========================================================================
 * Multiplier Support
 * ========================================================================== */

void
lrg_resource_pool_set_multiplier (LrgResourcePool *self,
                                  LrgResource     *resource,
                                  gdouble          multiplier)
{
    ResourceEntry *entry;

    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (multiplier >= 0.0);

    entry = get_or_create_entry (self, resource);
    entry->multiplier = multiplier;
}

gdouble
lrg_resource_pool_get_multiplier (LrgResourcePool *self,
                                  LrgResource     *resource)
{
    ResourceEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 1.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 1.0);

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (self->resources, id);

    if (entry != NULL)
        return entry->multiplier;

    return 1.0;
}

void
lrg_resource_pool_set_global_multiplier (LrgResourcePool *self,
                                         gdouble          multiplier)
{
    g_return_if_fail (LRG_IS_RESOURCE_POOL (self));
    g_return_if_fail (multiplier >= 0.0);

    if (self->global_multiplier != multiplier)
    {
        self->global_multiplier = multiplier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GLOBAL_MULTIPLIER]);
    }
}

gdouble
lrg_resource_pool_get_global_multiplier (LrgResourcePool *self)
{
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (self), 1.0);

    return self->global_multiplier;
}
