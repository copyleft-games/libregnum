/* lrg-producer.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-producer.h"
#include "../lrg-log.h"

typedef struct
{
    LrgProductionRecipe *recipe;
    LrgResourcePool     *resource_pool;   /* Output pool */
    LrgResourcePool     *input_pool;      /* Input pool (NULL = use resource_pool) */
    gdouble              rate_multiplier;
    gboolean             auto_restart;
    gboolean             is_producing;
    gdouble              elapsed_time;

    /* For cancel/refund tracking */
    gboolean             inputs_consumed;
} LrgProducerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (LrgProducer, lrg_producer, LRG_TYPE_COMPONENT)

enum
{
    PROP_0,
    PROP_RECIPE,
    PROP_RESOURCE_POOL,
    PROP_INPUT_POOL,
    PROP_RATE_MULTIPLIER,
    PROP_AUTO_RESTART,
    PROP_IS_PRODUCING,
    PROP_PROGRESS,
    N_PROPS
};

enum
{
    SIGNAL_PRODUCTION_STARTED,
    SIGNAL_PRODUCTION_COMPLETE,
    N_SIGNALS
};

static GParamSpec *properties[N_PROPS];
static guint signals[N_SIGNALS];

/* ==========================================================================
 * Internal Helpers
 * ========================================================================== */

static LrgResourcePool *
get_effective_input_pool (LrgProducer *self)
{
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);

    if (priv->input_pool != NULL)
        return priv->input_pool;

    return priv->resource_pool;
}

static gdouble
get_effective_production_time (LrgProducer *self)
{
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);
    gdouble base_time;

    if (priv->recipe == NULL)
        return 0.0;

    base_time = lrg_production_recipe_get_production_time (priv->recipe);

    if (priv->rate_multiplier <= 0.0)
        return base_time;

    return base_time / priv->rate_multiplier;
}

static void
complete_production (LrgProducer *self)
{
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);
    LrgProducerClass *klass = LRG_PRODUCER_GET_CLASS (self);
    g_autoptr(GList) outputs = NULL;
    GList *l;

    if (priv->recipe == NULL || priv->resource_pool == NULL)
        return;

    /* Add outputs (inputs were already consumed at start) */
    outputs = lrg_production_recipe_get_outputs (priv->recipe);
    for (l = outputs; l != NULL; l = l->next)
    {
        LrgResource *resource = LRG_RESOURCE (l->data);
        gdouble amount = lrg_production_recipe_get_output_amount (priv->recipe, resource);
        gdouble chance = lrg_production_recipe_get_output_chance (priv->recipe, resource);

        /* Roll for chance */
        if (chance >= 1.0 || g_random_double () <= chance)
        {
            lrg_resource_pool_add (priv->resource_pool, resource, amount);
        }
    }

    priv->is_producing = FALSE;
    priv->elapsed_time = 0.0;
    priv->inputs_consumed = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRODUCING]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    /* Call virtual method */
    if (klass->on_production_complete != NULL)
        klass->on_production_complete (self);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_PRODUCTION_COMPLETE], 0);

    /* Auto-restart if enabled */
    if (priv->auto_restart)
    {
        lrg_producer_start (self);
    }
}

/* ==========================================================================
 * Default Virtual Function Implementations
 * ========================================================================== */

static gboolean
lrg_producer_real_can_produce (LrgProducer *self)
{
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);
    LrgResourcePool *input_pool;

    if (priv->is_producing)
        return FALSE;

    if (priv->recipe == NULL)
        return FALSE;

    if (priv->resource_pool == NULL)
        return FALSE;

    if (!lrg_production_recipe_get_enabled (priv->recipe))
        return FALSE;

    input_pool = get_effective_input_pool (self);
    return lrg_production_recipe_can_produce (priv->recipe, input_pool);
}

/* ==========================================================================
 * LrgComponent Virtual Method Overrides
 * ========================================================================== */

static void
lrg_producer_update (LrgComponent *component,
                     gfloat        delta)
{
    LrgProducer *self = LRG_PRODUCER (component);
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);
    gdouble production_time;

    if (!priv->is_producing)
        return;

    priv->elapsed_time += (gdouble)delta;
    production_time = get_effective_production_time (self);

    /* Notify progress changed */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    /* Check if production is complete */
    if (priv->elapsed_time >= production_time)
    {
        complete_production (self);
    }
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_producer_finalize (GObject *object)
{
    LrgProducer *self = LRG_PRODUCER (object);
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);

    g_clear_object (&priv->recipe);
    g_clear_object (&priv->resource_pool);
    g_clear_object (&priv->input_pool);

    G_OBJECT_CLASS (lrg_producer_parent_class)->finalize (object);
}

static void
lrg_producer_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
    LrgProducer *self = LRG_PRODUCER (object);
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);

    switch (prop_id)
    {
    case PROP_RECIPE:
        g_value_set_object (value, priv->recipe);
        break;
    case PROP_RESOURCE_POOL:
        g_value_set_object (value, priv->resource_pool);
        break;
    case PROP_INPUT_POOL:
        g_value_set_object (value, priv->input_pool);
        break;
    case PROP_RATE_MULTIPLIER:
        g_value_set_double (value, priv->rate_multiplier);
        break;
    case PROP_AUTO_RESTART:
        g_value_set_boolean (value, priv->auto_restart);
        break;
    case PROP_IS_PRODUCING:
        g_value_set_boolean (value, priv->is_producing);
        break;
    case PROP_PROGRESS:
        g_value_set_double (value, lrg_producer_get_progress (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_producer_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
    LrgProducer *self = LRG_PRODUCER (object);

    switch (prop_id)
    {
    case PROP_RECIPE:
        lrg_producer_set_recipe (self, g_value_get_object (value));
        break;
    case PROP_RESOURCE_POOL:
        lrg_producer_set_resource_pool (self, g_value_get_object (value));
        break;
    case PROP_INPUT_POOL:
        lrg_producer_set_input_pool (self, g_value_get_object (value));
        break;
    case PROP_RATE_MULTIPLIER:
        lrg_producer_set_rate_multiplier (self, g_value_get_double (value));
        break;
    case PROP_AUTO_RESTART:
        lrg_producer_set_auto_restart (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_producer_class_init (LrgProducerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    LrgComponentClass *component_class = LRG_COMPONENT_CLASS (klass);

    object_class->finalize = lrg_producer_finalize;
    object_class->get_property = lrg_producer_get_property;
    object_class->set_property = lrg_producer_set_property;

    /* Override component update */
    component_class->update = lrg_producer_update;

    /* Virtual functions */
    klass->on_production_started = NULL;
    klass->on_production_complete = NULL;
    klass->can_produce = lrg_producer_real_can_produce;

    /**
     * LrgProducer:recipe:
     *
     * The production recipe to use.
     */
    properties[PROP_RECIPE] =
        g_param_spec_object ("recipe",
                             "Recipe",
                             "Production recipe",
                             LRG_TYPE_PRODUCTION_RECIPE,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:resource-pool:
     *
     * The resource pool for outputs.
     */
    properties[PROP_RESOURCE_POOL] =
        g_param_spec_object ("resource-pool",
                             "Resource Pool",
                             "Resource pool for outputs",
                             LRG_TYPE_RESOURCE_POOL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:input-pool:
     *
     * The resource pool for inputs (NULL to use resource-pool).
     */
    properties[PROP_INPUT_POOL] =
        g_param_spec_object ("input-pool",
                             "Input Pool",
                             "Resource pool for inputs",
                             LRG_TYPE_RESOURCE_POOL,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:rate-multiplier:
     *
     * Production speed multiplier (1.0 = normal, 2.0 = 2x speed).
     */
    properties[PROP_RATE_MULTIPLIER] =
        g_param_spec_double ("rate-multiplier",
                             "Rate Multiplier",
                             "Production speed multiplier",
                             0.01, 1000.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:auto-restart:
     *
     * Whether to automatically restart after completing.
     */
    properties[PROP_AUTO_RESTART] =
        g_param_spec_boolean ("auto-restart",
                              "Auto Restart",
                              "Automatically restart production",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:is-producing:
     *
     * Whether production is in progress.
     */
    properties[PROP_IS_PRODUCING] =
        g_param_spec_boolean ("is-producing",
                              "Is Producing",
                              "Whether production is in progress",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS |
                              G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgProducer:progress:
     *
     * Current production progress (0.0 to 1.0).
     */
    properties[PROP_PROGRESS] =
        g_param_spec_double ("progress",
                             "Progress",
                             "Production progress",
                             0.0, 1.0, 0.0,
                             G_PARAM_READABLE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgProducer::production-started:
     * @self: the producer
     *
     * Emitted when production starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PRODUCTION_STARTED] =
        g_signal_new ("production-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgProducerClass, on_production_started),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);

    /**
     * LrgProducer::production-complete:
     * @self: the producer
     *
     * Emitted when production completes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_PRODUCTION_COMPLETE] =
        g_signal_new ("production-complete",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      G_STRUCT_OFFSET (LrgProducerClass, on_production_complete),
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 0);
}

static void
lrg_producer_init (LrgProducer *self)
{
    LrgProducerPrivate *priv = lrg_producer_get_instance_private (self);

    priv->rate_multiplier = 1.0;
    priv->auto_restart = TRUE;
    priv->is_producing = FALSE;
    priv->elapsed_time = 0.0;
    priv->inputs_consumed = FALSE;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgProducer *
lrg_producer_new (void)
{
    return g_object_new (LRG_TYPE_PRODUCER, NULL);
}

LrgProducer *
lrg_producer_new_with_recipe (LrgProductionRecipe *recipe,
                              LrgResourcePool     *pool)
{
    return g_object_new (LRG_TYPE_PRODUCER,
                         "recipe", recipe,
                         "resource-pool", pool,
                         NULL);
}

/* ==========================================================================
 * Properties
 * ========================================================================== */

LrgProductionRecipe *
lrg_producer_get_recipe (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), NULL);

    priv = lrg_producer_get_instance_private (self);
    return priv->recipe;
}

void
lrg_producer_set_recipe (LrgProducer         *self,
                         LrgProductionRecipe *recipe)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));
    g_return_if_fail (recipe == NULL || LRG_IS_PRODUCTION_RECIPE (recipe));

    priv = lrg_producer_get_instance_private (self);

    if (g_set_object (&priv->recipe, recipe))
    {
        /* Stop production if recipe changes */
        if (priv->is_producing)
            lrg_producer_stop (self);

        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RECIPE]);
    }
}

LrgResourcePool *
lrg_producer_get_resource_pool (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), NULL);

    priv = lrg_producer_get_instance_private (self);
    return priv->resource_pool;
}

void
lrg_producer_set_resource_pool (LrgProducer     *self,
                                LrgResourcePool *pool)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));
    g_return_if_fail (pool == NULL || LRG_IS_RESOURCE_POOL (pool));

    priv = lrg_producer_get_instance_private (self);

    if (g_set_object (&priv->resource_pool, pool))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RESOURCE_POOL]);
    }
}

LrgResourcePool *
lrg_producer_get_input_pool (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), NULL);

    priv = lrg_producer_get_instance_private (self);
    return priv->input_pool;
}

void
lrg_producer_set_input_pool (LrgProducer     *self,
                             LrgResourcePool *pool)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));
    g_return_if_fail (pool == NULL || LRG_IS_RESOURCE_POOL (pool));

    priv = lrg_producer_get_instance_private (self);

    if (g_set_object (&priv->input_pool, pool))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INPUT_POOL]);
    }
}

gdouble
lrg_producer_get_rate_multiplier (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), 1.0);

    priv = lrg_producer_get_instance_private (self);
    return priv->rate_multiplier;
}

void
lrg_producer_set_rate_multiplier (LrgProducer *self,
                                  gdouble      multiplier)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));
    g_return_if_fail (multiplier > 0.0);

    priv = lrg_producer_get_instance_private (self);

    if (priv->rate_multiplier != multiplier)
    {
        priv->rate_multiplier = multiplier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RATE_MULTIPLIER]);
    }
}

gboolean
lrg_producer_get_auto_restart (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), TRUE);

    priv = lrg_producer_get_instance_private (self);
    return priv->auto_restart;
}

void
lrg_producer_set_auto_restart (LrgProducer *self,
                               gboolean     auto_restart)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));

    priv = lrg_producer_get_instance_private (self);

    if (priv->auto_restart != auto_restart)
    {
        priv->auto_restart = auto_restart;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_AUTO_RESTART]);
    }
}

/* ==========================================================================
 * State
 * ========================================================================== */

gboolean
lrg_producer_get_is_producing (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), FALSE);

    priv = lrg_producer_get_instance_private (self);
    return priv->is_producing;
}

gdouble
lrg_producer_get_progress (LrgProducer *self)
{
    LrgProducerPrivate *priv;
    gdouble production_time;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), 0.0);

    priv = lrg_producer_get_instance_private (self);

    if (!priv->is_producing)
        return 0.0;

    production_time = get_effective_production_time (self);

    if (production_time <= 0.0)
        return 1.0;

    return CLAMP (priv->elapsed_time / production_time, 0.0, 1.0);
}

gdouble
lrg_producer_get_elapsed_time (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), 0.0);

    priv = lrg_producer_get_instance_private (self);
    return priv->elapsed_time;
}

gdouble
lrg_producer_get_remaining_time (LrgProducer *self)
{
    LrgProducerPrivate *priv;
    gdouble production_time;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), 0.0);

    priv = lrg_producer_get_instance_private (self);

    if (!priv->is_producing)
        return 0.0;

    production_time = get_effective_production_time (self);
    return MAX (0.0, production_time - priv->elapsed_time);
}

/* ==========================================================================
 * Control
 * ========================================================================== */

gboolean
lrg_producer_start (LrgProducer *self)
{
    LrgProducerPrivate *priv;
    LrgProducerClass *klass;
    LrgResourcePool *input_pool;
    g_autoptr(GList) inputs = NULL;
    GList *l;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), FALSE);

    priv = lrg_producer_get_instance_private (self);
    klass = LRG_PRODUCER_GET_CLASS (self);

    /* Check if can produce */
    if (!lrg_producer_can_produce (self))
        return FALSE;

    /* Consume inputs at start */
    input_pool = get_effective_input_pool (self);
    inputs = lrg_production_recipe_get_inputs (priv->recipe);

    for (l = inputs; l != NULL; l = l->next)
    {
        LrgResource *resource = LRG_RESOURCE (l->data);
        gdouble amount = lrg_production_recipe_get_input_amount (priv->recipe, resource);

        if (!lrg_resource_pool_remove (input_pool, resource, amount))
        {
            /* Shouldn't happen since we checked can_produce, but be safe */
            return FALSE;
        }
    }

    priv->is_producing = TRUE;
    priv->elapsed_time = 0.0;
    priv->inputs_consumed = TRUE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRODUCING]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);

    /* Call virtual method */
    if (klass->on_production_started != NULL)
        klass->on_production_started (self);

    /* Emit signal */
    g_signal_emit (self, signals[SIGNAL_PRODUCTION_STARTED], 0);

    return TRUE;
}

void
lrg_producer_stop (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));

    priv = lrg_producer_get_instance_private (self);

    if (!priv->is_producing)
        return;

    priv->is_producing = FALSE;
    priv->elapsed_time = 0.0;
    priv->inputs_consumed = FALSE;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_IS_PRODUCING]);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PROGRESS]);
}

gboolean
lrg_producer_cancel (LrgProducer *self)
{
    LrgProducerPrivate *priv;
    LrgResourcePool *input_pool;
    g_autoptr(GList) inputs = NULL;
    GList *l;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), FALSE);

    priv = lrg_producer_get_instance_private (self);

    if (!priv->is_producing || !priv->inputs_consumed)
    {
        lrg_producer_stop (self);
        return FALSE;
    }

    /* Refund inputs */
    input_pool = get_effective_input_pool (self);
    inputs = lrg_production_recipe_get_inputs (priv->recipe);

    for (l = inputs; l != NULL; l = l->next)
    {
        LrgResource *resource = LRG_RESOURCE (l->data);
        gdouble amount = lrg_production_recipe_get_input_amount (priv->recipe, resource);
        lrg_resource_pool_add (input_pool, resource, amount);
    }

    lrg_producer_stop (self);
    return TRUE;
}

void
lrg_producer_complete_immediately (LrgProducer *self)
{
    LrgProducerPrivate *priv;

    g_return_if_fail (LRG_IS_PRODUCER (self));

    priv = lrg_producer_get_instance_private (self);

    if (!priv->is_producing)
        return;

    complete_production (self);
}

gboolean
lrg_producer_can_produce (LrgProducer *self)
{
    LrgProducerClass *klass;

    g_return_val_if_fail (LRG_IS_PRODUCER (self), FALSE);

    klass = LRG_PRODUCER_GET_CLASS (self);
    if (klass->can_produce != NULL)
        return klass->can_produce (self);

    return FALSE;
}
