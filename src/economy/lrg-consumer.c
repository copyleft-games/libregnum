/* lrg-consumer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-consumer.h"
#include "../lrg-log.h"

/* Requirement entry */
typedef struct
{
    LrgResource *resource;
    gdouble      rate;       /* Per second consumption */
    gboolean     starved;    /* Currently starved? */
} RequirementEntry;

static void
requirement_entry_free (gpointer data)
{
    RequirementEntry *entry = (RequirementEntry *)data;
    g_clear_object (&entry->resource);
    g_free (entry);
}

typedef struct
{
    LrgResourcePool *resource_pool;
    GHashTable      *requirements;    /* gchar* (id) -> RequirementEntry* */
    gdouble          rate_multiplier;
    gboolean         active;
    gboolean         is_starved;      /* Any requirement starved? */
} LrgConsumerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgConsumer, lrg_consumer, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_RESOURCE_POOL,
    PROP_RATE_MULTIPLIER,
    PROP_ACTIVE,
    PROP_IS_STARVED,
    PROP_SATISFACTION,
    N_PROPS
};

enum
{
    SIGNAL_STARVED,
    SIGNAL_SATISFIED,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* ==========================================================================
 * LrgComponent Virtual Method Overrides
 * ========================================================================== */

static void
lrg_consumer_update (LrgComponent *component,
                     gfloat        delta)
{
    LrgConsumer *self = LRG_CONSUMER (component);
    LrgConsumerPrivate *priv = lrg_consumer_get_instance_private (self);
    LrgConsumerClass *klass = LRG_CONSUMER_GET_CLASS (self);
    GHashTableIter iter;
    gpointer value;
    gboolean was_starved;
    gboolean now_starved = FALSE;
    gdouble effective_delta;

    if (!priv->active || priv->resource_pool == NULL)
        return;

    was_starved = priv->is_starved;
    effective_delta = (gdouble)delta * priv->rate_multiplier;

    /* Process each requirement */
    g_hash_table_iter_init (&iter, priv->requirements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        RequirementEntry *entry = (RequirementEntry *)value;
        gdouble amount_needed;
        gboolean resource_was_starved;

        amount_needed = entry->rate * effective_delta;
        resource_was_starved = entry->starved;

        /* Try to consume */
        if (lrg_resource_pool_has (priv->resource_pool, entry->resource, amount_needed))
        {
            lrg_resource_pool_remove (priv->resource_pool, entry->resource, amount_needed);
            entry->starved = FALSE;
        }
        else
        {
            /* Consume what we can */
            lrg_resource_pool_remove_clamped (priv->resource_pool,
                                               entry->resource,
                                               amount_needed);
            entry->starved = TRUE;
            now_starved = TRUE;

            /* Emit starved signal if newly starved */
            if (!resource_was_starved)
            {
                if (klass->on_starved != NULL)
                    klass->on_starved (self, entry->resource);

                g_signal_emit (self, signals[SIGNAL_STARVED], 0, entry->resource);
            }
        }
    }

    /* Update overall starved state */
    if (priv->is_starved != now_starved)
    {
        priv->is_starved = now_starved;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_STARVED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SATISFACTION]);

        /* Emit satisfied signal if no longer starved */
        if (was_starved && !now_starved)
        {
            if (klass->on_satisfied != NULL)
                klass->on_satisfied (self);

            g_signal_emit (self, signals[SIGNAL_SATISFIED], 0);
        }
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_consumer_finalize (GObject *object)
{
    LrgConsumer *self = LRG_CONSUMER (object);
    LrgConsumerPrivate *priv = lrg_consumer_get_instance_private (self);

    g_clear_object (&priv->resource_pool);
    g_clear_pointer (&priv->requirements, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_consumer_parent_class)->finalize (object);
}

static void
lrg_consumer_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgConsumer *self = LRG_CONSUMER (object);
    LrgConsumerPrivate *priv = lrg_consumer_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_RESOURCE_POOL:
        g_value_set_object (value, priv->resource_pool);
        break;
    case PROP_RATE_MULTIPLIER:
        g_value_set_double (value, priv->rate_multiplier);
        break;
    case PROP_ACTIVE:
        g_value_set_boolean (value, priv->active);
        break;
    case PROP_IS_STARVED:
        g_value_set_boolean (value, priv->is_starved);
        break;
    case PROP_SATISFACTION:
        g_value_set_double (value, lrg_consumer_get_satisfaction (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_consumer_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgConsumer *self = LRG_CONSUMER (object);

    switch (prop_id)
    {
    case PROP_RESOURCE_POOL:
        lrg_consumer_set_resource_pool (self, g_value_get_object (value));
        break;
    case PROP_RATE_MULTIPLIER:
        lrg_consumer_set_rate_multiplier (self, g_value_get_double (value));
        break;
    case PROP_ACTIVE:
        lrg_consumer_set_active (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_consumer_class_init (LrgConsumerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

    object_class->finalize = lrg_consumer_finalize;
    object_class->get_property = lrg_consumer_get_property;
    object_class->set_property = lrg_consumer_set_property;

    /* Override component update */
    component_class->update = lrg_consumer_update;

    /* Virtual functions */
    klass->on_starved = NULL;
    klass->on_satisfied = NULL;

    /**
     * LrgConsumer:resource-pool:
     *
     * The resource pool to consume from.
     */
    properties[PROP_RESOURCE_POOL] =
        g_param_spec_object ("resource-pool",
                             "Resource Pool",
                             "Resource pool to consume from",
                             LRG_TYPE_RESOURCE_POOL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgConsumer:rate-multiplier:
     *
     * Consumption rate multiplier (1.0 = normal, 2.0 = 2x consumption).
     */
    properties[PROP_RATE_MULTIPLIER] =
        g_param_spec_double ("rate-multiplier",
                             "Rate Multiplier",
                             "Consumption rate multiplier",
                             0.0, 1000.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgConsumer:active:
     *
     * Whether consumption is active.
     */
    properties[PROP_ACTIVE] =
        g_param_spec_boolean ("active",
                              "Active",
                              "Whether consuming resources",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgConsumer:is-starved:
     *
     * Whether any requirement is not being met.
     */
    properties[PROP_IS_STARVED] =
        g_param_spec_boolean ("is-starved",
                              "Is Starved",
                              "Whether any requirement is not met",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgConsumer:satisfaction:
     *
     * Overall satisfaction level (0.0 = all starved, 1.0 = all met).
     */
    properties[PROP_SATISFACTION] =
        g_param_spec_double ("satisfaction",
                             "Satisfaction",
                             "Overall satisfaction level",
                             0.0, 1.0, 1.0,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgConsumer::starved:
     * @self: the consumer
     * @resource: the resource that ran out
     *
     * Emitted when a required resource is depleted.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STARVED] =
        g_signal_new ("starved",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgConsumerClass, on_starved),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_RESOURCE);

    /**
     * LrgConsumer::satisfied:
     * @self: the consumer
     *
     * Emitted when all requirements are being met again.
     *
     * Since: 1.0
     */
    signals[SIGNAL_SATISFIED] =
        g_signal_new ("satisfied",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgConsumerClass, on_satisfied),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_consumer_init (LrgConsumer *self)
{
    LrgConsumerPrivate *priv = lrg_consumer_get_instance_private (self);

    priv->requirements = g_hash_table_new_full (g_str_hash,
                                                 g_str_equal,
                                                 g_free,
                                                 requirement_entry_free);
    priv->rate_multiplier = 1.0;
    priv->active = TRUE;
    priv->is_starved = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgConsumer *
lrg_consumer_new (void)
{
    return g_object_new (LRG_TYPE_CONSUMER, NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgResourcePool *
lrg_consumer_get_resource_pool (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), NULL);

    priv = lrg_consumer_get_instance_private (self);
    return priv->resource_pool;
}

void
lrg_consumer_set_resource_pool (LrgConsumer     *self,
                                LrgResourcePool *pool)
{
    LrgConsumerPrivate *priv;

    g_return_if_fail (LRG_IS_CONSUMER (self));
    g_return_if_fail (pool == NULL || LRG_IS_RESOURCE_POOL (pool));

    priv = lrg_consumer_get_instance_private (self);

    if (g_set_object (&priv->resource_pool, pool))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESOURCE_POOL]);
    }
}

gdouble
lrg_consumer_get_rate_multiplier (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), 1.0);

    priv = lrg_consumer_get_instance_private (self);
    return priv->rate_multiplier;
}

void
lrg_consumer_set_rate_multiplier (LrgConsumer *self,
                                  gdouble      multiplier)
{
    LrgConsumerPrivate *priv;

    g_return_if_fail (LRG_IS_CONSUMER (self));
    g_return_if_fail (multiplier >= 0.0);

    priv = lrg_consumer_get_instance_private (self);

    if (priv->rate_multiplier != multiplier)
    {
        priv->rate_multiplier = multiplier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RATE_MULTIPLIER]);
    }
}

gboolean
lrg_consumer_get_active (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), FALSE);

    priv = lrg_consumer_get_instance_private (self);
    return priv->active;
}

void
lrg_consumer_set_active (LrgConsumer *self,
                         gboolean     active)
{
    LrgConsumerPrivate *priv;

    g_return_if_fail (LRG_IS_CONSUMER (self));

    priv = lrg_consumer_get_instance_private (self);

    if (priv->active != active)
    {
        priv->active = active;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

/* ==========================================================================
 * Requirements
 * ========================================================================== */

void
lrg_consumer_add_requirement (LrgConsumer *self,
                              LrgResource *resource,
                              gdouble      rate)
{
    LrgConsumerPrivate *priv;
    RequirementEntry *entry;
    const gchar *id;

    g_return_if_fail (LRG_IS_CONSUMER (self));
    g_return_if_fail (LRG_IS_RESOURCE (resource));
    g_return_if_fail (rate > 0.0);

    priv = lrg_consumer_get_instance_private (self);

    entry = g_new0 (RequirementEntry, 1);
    entry->resource = g_object_ref (resource);
    entry->rate = rate;
    entry->starved = FALSE;

    id = lrg_resource_get_id (resource);
    g_hash_table_insert (priv->requirements, g_strdup (id), entry);
}

gboolean
lrg_consumer_remove_requirement (LrgConsumer *self,
                                 LrgResource *resource)
{
    LrgConsumerPrivate *priv;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    priv = lrg_consumer_get_instance_private (self);
    id = lrg_resource_get_id (resource);

    return g_hash_table_remove (priv->requirements, id);
}

gdouble
lrg_consumer_get_requirement_rate (LrgConsumer *self,
                                   LrgResource *resource)
{
    LrgConsumerPrivate *priv;
    RequirementEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), 0.0);

    priv = lrg_consumer_get_instance_private (self);
    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (priv->requirements, id);

    if (entry != NULL)
        return entry->rate;

    return 0.0;
}

GList *
lrg_consumer_get_requirements (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;
    GList *result = NULL;
    GHashTableIter iter;
    gpointer value;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), NULL);

    priv = lrg_consumer_get_instance_private (self);

    g_hash_table_iter_init (&iter, priv->requirements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        RequirementEntry *entry = (RequirementEntry *)value;
        result = g_list_prepend (result, entry->resource);
    }

    return result;
}

guint
lrg_consumer_get_requirement_count (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), 0);

    priv = lrg_consumer_get_instance_private (self);
    return g_hash_table_size (priv->requirements);
}

void
lrg_consumer_clear_requirements (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_if_fail (LRG_IS_CONSUMER (self));

    priv = lrg_consumer_get_instance_private (self);
    g_hash_table_remove_all (priv->requirements);

    /* Reset starved state */
    if (priv->is_starved)
    {
        priv->is_starved = FALSE;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_STARVED]);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SATISFACTION]);
    }
}

/* ==========================================================================
 * State
 * ========================================================================== */

gboolean
lrg_consumer_is_starved (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), FALSE);

    priv = lrg_consumer_get_instance_private (self);
    return priv->is_starved;
}

gboolean
lrg_consumer_is_resource_starved (LrgConsumer *self,
                                  LrgResource *resource)
{
    LrgConsumerPrivate *priv;
    RequirementEntry *entry;
    const gchar *id;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), FALSE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), FALSE);

    priv = lrg_consumer_get_instance_private (self);
    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (priv->requirements, id);

    if (entry != NULL)
        return entry->starved;

    return FALSE;
}

gdouble
lrg_consumer_get_satisfaction (LrgConsumer *self)
{
    LrgConsumerPrivate *priv;
    GHashTableIter iter;
    gpointer value;
    guint total = 0;
    guint satisfied = 0;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), 1.0);

    priv = lrg_consumer_get_instance_private (self);

    if (g_hash_table_size (priv->requirements) == 0)
        return 1.0;

    g_hash_table_iter_init (&iter, priv->requirements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        RequirementEntry *entry = (RequirementEntry *)value;
        total++;
        if (!entry->starved)
            satisfied++;
    }

    if (total == 0)
        return 1.0;

    return (gdouble)satisfied / (gdouble)total;
}

gdouble
lrg_consumer_get_time_until_starved (LrgConsumer *self,
                                     LrgResource *resource)
{
    LrgConsumerPrivate *priv;
    RequirementEntry *entry;
    const gchar *id;
    gdouble available;
    gdouble effective_rate;

    g_return_val_if_fail (LRG_IS_CONSUMER (self), G_MAXDOUBLE);
    g_return_val_if_fail (LRG_IS_RESOURCE (resource), G_MAXDOUBLE);

    priv = lrg_consumer_get_instance_private (self);

    if (priv->resource_pool == NULL || !priv->active)
        return G_MAXDOUBLE;

    id = lrg_resource_get_id (resource);
    entry = g_hash_table_lookup (priv->requirements, id);

    if (entry == NULL)
        return G_MAXDOUBLE;

    available = lrg_resource_pool_get (priv->resource_pool, resource);
    effective_rate = entry->rate * priv->rate_multiplier;

    if (effective_rate <= 0.0)
        return G_MAXDOUBLE;

    return available / effective_rate;
}
