/* lrg-offline-calculator.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_ECONOMY

#include "config.h"
#include "lrg-offline-calculator.h"
#include "../lrg-log.h"

struct _LrgOfflineCalculator
{
    GObject     parent_instance;

    GPtrArray  *producers;       /* LrgProducer* array */
    gint64      snapshot_time;   /* Unix timestamp of last snapshot */

    /* Settings */
    gdouble     efficiency;      /* 0.0 to 1.0 multiplier */
    gdouble     max_hours;       /* Maximum offline hours to calculate */
    gdouble     min_seconds;     /* Minimum seconds before offline kicks in */
};

G_DEFINE_TYPE (LrgOfflineCalculator, lrg_offline_calculator, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_EFFICIENCY,
    PROP_MAX_HOURS,
    PROP_MIN_SECONDS,
    PROP_SNAPSHOT_TIME,
    PROP_PRODUCER_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_offline_calculator_finalize (GObject *object)
{
    LrgOfflineCalculator *self = LRG_OFFLINE_CALCULATOR (object);

    g_clear_pointer (&self->producers, g_ptr_array_unref);

    G_OBJECT_CLASS (lrg_offline_calculator_parent_class)->finalize (object);
}

static void
lrg_offline_calculator_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
    LrgOfflineCalculator *self = LRG_OFFLINE_CALCULATOR (object);

    switch (prop_id)
    {
    case PROP_EFFICIENCY:
        g_value_set_double (value, self->efficiency);
        break;
    case PROP_MAX_HOURS:
        g_value_set_double (value, self->max_hours);
        break;
    case PROP_MIN_SECONDS:
        g_value_set_double (value, self->min_seconds);
        break;
    case PROP_SNAPSHOT_TIME:
        g_value_set_int64 (value, self->snapshot_time);
        break;
    case PROP_PRODUCER_COUNT:
        g_value_set_uint (value, self->producers->len);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_offline_calculator_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
    LrgOfflineCalculator *self = LRG_OFFLINE_CALCULATOR (object);

    switch (prop_id)
    {
    case PROP_EFFICIENCY:
        lrg_offline_calculator_set_efficiency (self, g_value_get_double (value));
        break;
    case PROP_MAX_HOURS:
        lrg_offline_calculator_set_max_hours (self, g_value_get_double (value));
        break;
    case PROP_MIN_SECONDS:
        lrg_offline_calculator_set_min_seconds (self, g_value_get_double (value));
        break;
    case PROP_SNAPSHOT_TIME:
        lrg_offline_calculator_set_snapshot_time (self, g_value_get_int64 (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_offline_calculator_class_init (LrgOfflineCalculatorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_offline_calculator_finalize;
    object_class->get_property = lrg_offline_calculator_get_property;
    object_class->set_property = lrg_offline_calculator_set_property;

    /**
     * LrgOfflineCalculator:efficiency:
     *
     * The offline efficiency multiplier (0.0 to 1.0).
     * A value of 1.0 means 100% of normal production.
     */
    properties[PROP_EFFICIENCY] =
        g_param_spec_double ("efficiency",
                             "Efficiency",
                             "Offline efficiency multiplier",
                             0.0, 1.0, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgOfflineCalculator:max-hours:
     *
     * Maximum hours of offline progress to calculate.
     * Use G_MAXDOUBLE for unlimited.
     */
    properties[PROP_MAX_HOURS] =
        g_param_spec_double ("max-hours",
                             "Max Hours",
                             "Maximum offline hours",
                             0.0, G_MAXDOUBLE, 24.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgOfflineCalculator:min-seconds:
     *
     * Minimum seconds before offline progress kicks in.
     */
    properties[PROP_MIN_SECONDS] =
        g_param_spec_double ("min-seconds",
                             "Min Seconds",
                             "Minimum offline seconds",
                             0.0, G_MAXDOUBLE, 60.0,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS |
                             G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgOfflineCalculator:snapshot-time:
     *
     * Unix timestamp of the last snapshot.
     */
    properties[PROP_SNAPSHOT_TIME] =
        g_param_spec_int64 ("snapshot-time",
                            "Snapshot Time",
                            "Unix timestamp of snapshot",
                            0, G_MAXINT64, 0,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS |
                            G_PARAM_EXPLICIT_NOTIFY);

    /**
     * LrgOfflineCalculator:producer-count:
     *
     * Number of tracked producers.
     */
    properties[PROP_PRODUCER_COUNT] =
        g_param_spec_uint ("producer-count",
                           "Producer Count",
                           "Number of tracked producers",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_offline_calculator_init (LrgOfflineCalculator *self)
{
    self->producers = g_ptr_array_new_with_free_func (g_object_unref);
    self->snapshot_time = 0;
    self->efficiency = 1.0;
    self->max_hours = 24.0;
    self->min_seconds = 60.0;
}

/* ==========================================================================
 * Construction
 * ========================================================================== */

LrgOfflineCalculator *
lrg_offline_calculator_new (void)
{
    return g_object_new (LRG_TYPE_OFFLINE_CALCULATOR, NULL);
}

/* ==========================================================================
 * Producer Registration
 * ========================================================================== */

void
lrg_offline_calculator_add_producer (LrgOfflineCalculator *self,
                                     LrgProducer          *producer)
{
    guint i;

    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));
    g_return_if_fail (LRG_IS_PRODUCER (producer));

    /* Check if already tracked */
    for (i = 0; i < self->producers->len; i++)
    {
        if (g_ptr_array_index (self->producers, i) == producer)
            return;
    }

    g_ptr_array_add (self->producers, g_object_ref (producer));
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRODUCER_COUNT]);

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
               "Added producer to offline calculator (count: %u)",
               self->producers->len);
}

gboolean
lrg_offline_calculator_remove_producer (LrgOfflineCalculator *self,
                                        LrgProducer          *producer)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), FALSE);
    g_return_val_if_fail (LRG_IS_PRODUCER (producer), FALSE);

    if (g_ptr_array_remove (self->producers, producer))
    {
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRODUCER_COUNT]);
        lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
                   "Removed producer from offline calculator (count: %u)",
                   self->producers->len);
        return TRUE;
    }

    return FALSE;
}

void
lrg_offline_calculator_clear_producers (LrgOfflineCalculator *self)
{
    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));

    if (self->producers->len > 0)
    {
        g_ptr_array_set_size (self->producers, 0);
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRODUCER_COUNT]);
        lrg_debug (LRG_LOG_DOMAIN_ECONOMY, "Cleared all producers from offline calculator");
    }
}

guint
lrg_offline_calculator_get_producer_count (LrgOfflineCalculator *self)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 0);
    return self->producers->len;
}

/* ==========================================================================
 * Snapshot
 * ========================================================================== */

void
lrg_offline_calculator_take_snapshot (LrgOfflineCalculator *self)
{
    g_autoptr(GDateTime) now = NULL;

    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));

    now = g_date_time_new_now_utc ();
    self->snapshot_time = g_date_time_to_unix (now);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAPSHOT_TIME]);

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
               "Took offline snapshot at %" G_GINT64_FORMAT,
               self->snapshot_time);
}

gint64
lrg_offline_calculator_get_snapshot_time (LrgOfflineCalculator *self)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 0);
    return self->snapshot_time;
}

void
lrg_offline_calculator_set_snapshot_time (LrgOfflineCalculator *self,
                                          gint64                timestamp)
{
    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));

    if (self->snapshot_time != timestamp)
    {
        self->snapshot_time = timestamp;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAPSHOT_TIME]);
    }
}

/* ==========================================================================
 * Calculation
 * ========================================================================== */

/*
 * Helper to simulate one producer for a given duration.
 * Calculates how many complete production cycles would occur
 * and adds the outputs to the result pool.
 */
static void
simulate_producer (LrgProducer     *producer,
                   gdouble          duration,
                   gdouble          efficiency,
                   LrgResourcePool *result_pool)
{
    LrgProductionRecipe *recipe;
    gdouble production_time;
    gdouble rate_multiplier;
    gdouble effective_duration;
    gdouble cycles;
    g_autoptr(GList) outputs = NULL;
    GList *l;

    recipe = lrg_producer_get_recipe (producer);
    if (recipe == NULL)
        return;

    production_time = lrg_production_recipe_get_production_time (recipe);
    if (production_time <= 0.0)
        return;

    rate_multiplier = lrg_producer_get_rate_multiplier (producer);
    if (rate_multiplier <= 0.0)
        return;

    /* Apply efficiency and rate multiplier to duration */
    effective_duration = duration * efficiency * rate_multiplier;

    /* Calculate complete cycles */
    cycles = effective_duration / production_time;
    if (cycles < 1.0)
        return;

    /* Add outputs for each cycle */
    outputs = lrg_production_recipe_get_outputs (recipe);
    for (l = outputs; l != NULL; l = l->next)
    {
        LrgResource *resource = LRG_RESOURCE (l->data);
        gdouble amount = lrg_production_recipe_get_output_amount (recipe, resource);
        gdouble chance = lrg_production_recipe_get_output_chance (recipe, resource);

        /*
         * For offline calculation, we use expected value:
         * total = cycles * amount * chance
         * This smooths out the randomness over long periods.
         */
        gdouble total = cycles * amount * chance;

        lrg_resource_pool_add (result_pool, resource, total);

        lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
                   "Offline: %.2f cycles of %s -> %.2f %s",
                   cycles,
                   lrg_production_recipe_get_id (recipe),
                   total,
                   lrg_resource_get_id (resource));
    }
}

gdouble
lrg_offline_calculator_calculate (LrgOfflineCalculator *self,
                                  LrgResourcePool      *result_pool)
{
    g_autoptr(GDateTime) now = NULL;
    gint64 current_time;
    gdouble duration;

    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (result_pool), 0.0);

    /* No snapshot taken */
    if (self->snapshot_time == 0)
        return 0.0;

    /* Calculate elapsed time */
    now = g_date_time_new_now_utc ();
    current_time = g_date_time_to_unix (now);
    duration = (gdouble)(current_time - self->snapshot_time);

    /* Check minimum threshold */
    if (duration < self->min_seconds)
    {
        lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
                   "Offline duration %.2fs below minimum %.2fs",
                   duration, self->min_seconds);
        return 0.0;
    }

    /* Cap to maximum hours */
    if (self->max_hours < G_MAXDOUBLE)
    {
        gdouble max_seconds = self->max_hours * 3600.0;
        if (duration > max_seconds)
        {
            lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
                       "Capping offline duration from %.2fs to %.2fs",
                       duration, max_seconds);
            duration = max_seconds;
        }
    }

    /* Calculate production for this duration */
    lrg_offline_calculator_calculate_duration (self, duration, result_pool);

    return duration;
}

void
lrg_offline_calculator_calculate_duration (LrgOfflineCalculator *self,
                                           gdouble               duration,
                                           LrgResourcePool      *result_pool)
{
    guint i;

    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));
    g_return_if_fail (duration >= 0.0);
    g_return_if_fail (LRG_IS_RESOURCE_POOL (result_pool));

    if (duration == 0.0)
        return;

    if (self->producers->len == 0)
    {
        lrg_debug (LRG_LOG_DOMAIN_ECONOMY, "No producers to simulate offline");
        return;
    }

    lrg_debug (LRG_LOG_DOMAIN_ECONOMY,
               "Calculating offline progress for %.2fs with %u producers",
               duration, self->producers->len);

    /* Simulate each producer */
    for (i = 0; i < self->producers->len; i++)
    {
        LrgProducer *producer = g_ptr_array_index (self->producers, i);
        simulate_producer (producer, duration, self->efficiency, result_pool);
    }
}

gdouble
lrg_offline_calculator_apply (LrgOfflineCalculator *self,
                              LrgResourcePool      *pool)
{
    g_autoptr(LrgResourcePool) result_pool = NULL;
    GList *resources;
    GList *iter;
    gdouble duration;

    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 0.0);
    g_return_val_if_fail (LRG_IS_RESOURCE_POOL (pool), 0.0);

    /* Create temporary pool to collect results */
    result_pool = lrg_resource_pool_new ();

    /* Calculate offline gains */
    duration = lrg_offline_calculator_calculate (self, result_pool);

    if (duration > 0.0)
    {
        /* Transfer results to the target pool */
        resources = lrg_resource_pool_get_resources (result_pool);
        for (iter = resources; iter != NULL; iter = iter->next)
        {
            LrgResource *resource = LRG_RESOURCE (iter->data);
            gdouble amount = lrg_resource_pool_get (result_pool, resource);
            lrg_resource_pool_add (pool, resource, amount);
        }
        g_list_free (resources);

        lrg_info (LRG_LOG_DOMAIN_ECONOMY,
                  "Applied offline progress: %.2f hours",
                  duration / 3600.0);
    }

    return duration;
}

/* ==========================================================================
 * Settings
 * ========================================================================== */

gdouble
lrg_offline_calculator_get_efficiency (LrgOfflineCalculator *self)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 1.0);
    return self->efficiency;
}

void
lrg_offline_calculator_set_efficiency (LrgOfflineCalculator *self,
                                       gdouble               efficiency)
{
    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));

    efficiency = CLAMP (efficiency, 0.0, 1.0);

    if (self->efficiency != efficiency)
    {
        self->efficiency = efficiency;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_EFFICIENCY]);
    }
}

gdouble
lrg_offline_calculator_get_max_hours (LrgOfflineCalculator *self)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 24.0);
    return self->max_hours;
}

void
lrg_offline_calculator_set_max_hours (LrgOfflineCalculator *self,
                                      gdouble               max_hours)
{
    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));
    g_return_if_fail (max_hours >= 0.0);

    if (self->max_hours != max_hours)
    {
        self->max_hours = max_hours;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_HOURS]);
    }
}

gdouble
lrg_offline_calculator_get_min_seconds (LrgOfflineCalculator *self)
{
    g_return_val_if_fail (LRG_IS_OFFLINE_CALCULATOR (self), 60.0);
    return self->min_seconds;
}

void
lrg_offline_calculator_set_min_seconds (LrgOfflineCalculator *self,
                                        gdouble               min_seconds)
{
    g_return_if_fail (LRG_IS_OFFLINE_CALCULATOR (self));
    g_return_if_fail (min_seconds >= 0.0);

    if (self->min_seconds != min_seconds)
    {
        self->min_seconds = min_seconds;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MIN_SECONDS]);
    }
}
