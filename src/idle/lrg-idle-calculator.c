/* lrg-idle-calculator.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "config.h"
#include "lrg-idle-calculator.h"

/* ========================================================================= */
/* LrgIdleGenerator - Boxed type for production generators                  */
/* ========================================================================= */

/* Forward declarations for G_DEFINE_BOXED_TYPE */
LrgIdleGenerator *lrg_idle_generator_copy (const LrgIdleGenerator *self);
void lrg_idle_generator_free (LrgIdleGenerator *self);

G_DEFINE_BOXED_TYPE (LrgIdleGenerator, lrg_idle_generator,
                     lrg_idle_generator_copy,
                     lrg_idle_generator_free)

LrgIdleGenerator *
lrg_idle_generator_new (const gchar        *id,
                        const LrgBigNumber *base_rate)
{
    LrgIdleGenerator *self;

    g_return_val_if_fail (id != NULL, NULL);
    g_return_val_if_fail (base_rate != NULL, NULL);

    self = g_slice_new0 (LrgIdleGenerator);
    self->id = g_strdup (id);
    self->base_rate = lrg_big_number_copy (base_rate);
    self->count = 0;
    self->multiplier = 1.0;
    self->enabled = TRUE;

    return self;
}

LrgIdleGenerator *
lrg_idle_generator_new_simple (const gchar *id,
                               gdouble      base_rate)
{
    g_autoptr(LrgBigNumber) rate = NULL;

    rate = lrg_big_number_new (base_rate);
    return lrg_idle_generator_new (id, rate);
}

LrgIdleGenerator *
lrg_idle_generator_copy (const LrgIdleGenerator *self)
{
    LrgIdleGenerator *copy;

    g_return_val_if_fail (self != NULL, NULL);

    copy = g_slice_new0 (LrgIdleGenerator);
    copy->id = g_strdup (self->id);
    copy->base_rate = lrg_big_number_copy (self->base_rate);
    copy->count = self->count;
    copy->multiplier = self->multiplier;
    copy->enabled = self->enabled;

    return copy;
}

void
lrg_idle_generator_free (LrgIdleGenerator *self)
{
    if (self == NULL)
        return;

    g_free (self->id);
    lrg_big_number_free (self->base_rate);
    g_slice_free (LrgIdleGenerator, self);
}

const gchar *
lrg_idle_generator_get_id (const LrgIdleGenerator *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const LrgBigNumber *
lrg_idle_generator_get_base_rate (const LrgIdleGenerator *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->base_rate;
}

gint64
lrg_idle_generator_get_count (const LrgIdleGenerator *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->count;
}

void
lrg_idle_generator_set_count (LrgIdleGenerator *self,
                              gint64            count)
{
    g_return_if_fail (self != NULL);
    self->count = count;
}

gdouble
lrg_idle_generator_get_multiplier (const LrgIdleGenerator *self)
{
    g_return_val_if_fail (self != NULL, 1.0);
    return self->multiplier;
}

void
lrg_idle_generator_set_multiplier (LrgIdleGenerator *self,
                                   gdouble           multiplier)
{
    g_return_if_fail (self != NULL);
    self->multiplier = multiplier;
}

gboolean
lrg_idle_generator_is_enabled (const LrgIdleGenerator *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->enabled;
}

void
lrg_idle_generator_set_enabled (LrgIdleGenerator *self,
                                gboolean          enabled)
{
    g_return_if_fail (self != NULL);
    self->enabled = enabled;
}

LrgBigNumber *
lrg_idle_generator_get_effective_rate (const LrgIdleGenerator *self)
{
    g_autoptr(LrgBigNumber) rate = NULL;
    gdouble effective_mult;

    g_return_val_if_fail (self != NULL, lrg_big_number_new_zero ());

    if (!self->enabled || self->count <= 0)
        return lrg_big_number_new_zero ();

    /* effective = base_rate * count * multiplier */
    effective_mult = (gdouble)self->count * self->multiplier;
    rate = lrg_big_number_multiply_scalar (self->base_rate, effective_mult);

    return g_steal_pointer (&rate);
}

/* ========================================================================= */
/* LrgIdleCalculator - GObject for idle game calculations                   */
/* ========================================================================= */

struct _LrgIdleCalculator
{
    GObject    parent_instance;

    GPtrArray *generators;
    gdouble    global_multiplier;
    gint64     snapshot_time;
};

enum
{
    PROP_0,
    PROP_GLOBAL_MULTIPLIER,
    PROP_SNAPSHOT_TIME,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

G_DEFINE_TYPE (LrgIdleCalculator, lrg_idle_calculator, G_TYPE_OBJECT)

static void
lrg_idle_calculator_finalize (GObject *object)
{
    LrgIdleCalculator *self = LRG_IDLE_CALCULATOR (object);

    g_ptr_array_unref (self->generators);

    G_OBJECT_CLASS (lrg_idle_calculator_parent_class)->finalize (object);
}

static void
lrg_idle_calculator_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    LrgIdleCalculator *self = LRG_IDLE_CALCULATOR (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_MULTIPLIER:
        g_value_set_double (value, self->global_multiplier);
        break;

    case PROP_SNAPSHOT_TIME:
        g_value_set_int64 (value, self->snapshot_time);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_idle_calculator_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    LrgIdleCalculator *self = LRG_IDLE_CALCULATOR (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_MULTIPLIER:
        lrg_idle_calculator_set_global_multiplier (self, g_value_get_double (value));
        break;

    case PROP_SNAPSHOT_TIME:
        lrg_idle_calculator_set_snapshot_time (self, g_value_get_int64 (value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_idle_calculator_class_init (LrgIdleCalculatorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_idle_calculator_finalize;
    object_class->get_property = lrg_idle_calculator_get_property;
    object_class->set_property = lrg_idle_calculator_set_property;

    /**
     * LrgIdleCalculator:global-multiplier:
     *
     * Global multiplier applied to all production.
     *
     * Since: 1.0
     */
    properties[PROP_GLOBAL_MULTIPLIER] =
        g_param_spec_double ("global-multiplier",
                             "Global Multiplier",
                             "Global multiplier for all production",
                             0.0, G_MAXDOUBLE, 1.0,
                             G_PARAM_READWRITE |
                             G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    /**
     * LrgIdleCalculator:snapshot-time:
     *
     * Unix timestamp of the last snapshot.
     *
     * Since: 1.0
     */
    properties[PROP_SNAPSHOT_TIME] =
        g_param_spec_int64 ("snapshot-time",
                            "Snapshot Time",
                            "Unix timestamp of last snapshot",
                            0, G_MAXINT64, 0,
                            G_PARAM_READWRITE |
                            G_PARAM_EXPLICIT_NOTIFY |
                            G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_idle_calculator_init (LrgIdleCalculator *self)
{
    self->generators = g_ptr_array_new_with_free_func (
        (GDestroyNotify)lrg_idle_generator_free);
    self->global_multiplier = 1.0;
    self->snapshot_time = 0;
}

LrgIdleCalculator *
lrg_idle_calculator_new (void)
{
    return g_object_new (LRG_TYPE_IDLE_CALCULATOR, NULL);
}

void
lrg_idle_calculator_add_generator (LrgIdleCalculator      *self,
                                   const LrgIdleGenerator *generator)
{
    g_return_if_fail (LRG_IS_IDLE_CALCULATOR (self));
    g_return_if_fail (generator != NULL);

    g_ptr_array_add (self->generators, lrg_idle_generator_copy (generator));
}

gboolean
lrg_idle_calculator_remove_generator (LrgIdleCalculator *self,
                                      const gchar       *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    for (i = 0; i < self->generators->len; i++)
    {
        LrgIdleGenerator *gen = g_ptr_array_index (self->generators, i);
        if (g_strcmp0 (gen->id, id) == 0)
        {
            g_ptr_array_remove_index (self->generators, i);
            return TRUE;
        }
    }

    return FALSE;
}

LrgIdleGenerator *
lrg_idle_calculator_get_generator (LrgIdleCalculator *self,
                                   const gchar       *id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    for (i = 0; i < self->generators->len; i++)
    {
        LrgIdleGenerator *gen = g_ptr_array_index (self->generators, i);
        if (g_strcmp0 (gen->id, id) == 0)
            return gen;
    }

    return NULL;
}

GPtrArray *
lrg_idle_calculator_get_generators (LrgIdleCalculator *self)
{
    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), NULL);
    return self->generators;
}

gdouble
lrg_idle_calculator_get_global_multiplier (LrgIdleCalculator *self)
{
    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), 1.0);
    return self->global_multiplier;
}

void
lrg_idle_calculator_set_global_multiplier (LrgIdleCalculator *self,
                                           gdouble            multiplier)
{
    g_return_if_fail (LRG_IS_IDLE_CALCULATOR (self));

    if (self->global_multiplier != multiplier)
    {
        self->global_multiplier = multiplier;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GLOBAL_MULTIPLIER]);
    }
}

LrgBigNumber *
lrg_idle_calculator_get_total_rate (LrgIdleCalculator *self)
{
    g_autoptr(LrgBigNumber) total = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), lrg_big_number_new_zero ());

    total = lrg_big_number_new_zero ();

    for (i = 0; i < self->generators->len; i++)
    {
        LrgIdleGenerator *gen = g_ptr_array_index (self->generators, i);
        g_autoptr(LrgBigNumber) rate = lrg_idle_generator_get_effective_rate (gen);

        lrg_big_number_add_in_place (total, rate);
    }

    /* Apply global multiplier */
    lrg_big_number_multiply_in_place (total, self->global_multiplier);

    return g_steal_pointer (&total);
}

LrgBigNumber *
lrg_idle_calculator_simulate (LrgIdleCalculator *self,
                              gdouble            seconds)
{
    g_autoptr(LrgBigNumber) rate = NULL;
    g_autoptr(LrgBigNumber) result = NULL;

    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), lrg_big_number_new_zero ());

    if (seconds <= 0.0)
        return lrg_big_number_new_zero ();

    rate = lrg_idle_calculator_get_total_rate (self);
    result = lrg_big_number_multiply_scalar (rate, seconds);

    return g_steal_pointer (&result);
}

LrgBigNumber *
lrg_idle_calculator_simulate_offline (LrgIdleCalculator *self,
                                      gint64             last_active_time,
                                      gdouble            efficiency,
                                      gdouble            max_hours)
{
    g_autoptr(GDateTime) now = NULL;
    g_autoptr(LrgBigNumber) production = NULL;
    gint64 current_time;
    gdouble elapsed_seconds;

    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), lrg_big_number_new_zero ());

    if (last_active_time <= 0)
        return lrg_big_number_new_zero ();

    /* Calculate elapsed time */
    now = g_date_time_new_now_utc ();
    current_time = g_date_time_to_unix (now);
    elapsed_seconds = (gdouble)(current_time - last_active_time);

    if (elapsed_seconds <= 0.0)
        return lrg_big_number_new_zero ();

    /* Apply max hours cap if specified */
    if (max_hours > 0.0)
    {
        gdouble max_seconds = max_hours * 3600.0;
        if (elapsed_seconds > max_seconds)
            elapsed_seconds = max_seconds;
    }

    /* Calculate production */
    production = lrg_idle_calculator_simulate (self, elapsed_seconds);

    /* Apply offline efficiency */
    if (efficiency >= 0.0 && efficiency < 1.0)
        lrg_big_number_multiply_in_place (production, efficiency);

    return g_steal_pointer (&production);
}

void
lrg_idle_calculator_take_snapshot (LrgIdleCalculator *self)
{
    g_autoptr(GDateTime) now = NULL;

    g_return_if_fail (LRG_IS_IDLE_CALCULATOR (self));

    now = g_date_time_new_now_utc ();
    self->snapshot_time = g_date_time_to_unix (now);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAPSHOT_TIME]);
}

gint64
lrg_idle_calculator_get_snapshot_time (LrgIdleCalculator *self)
{
    g_return_val_if_fail (LRG_IS_IDLE_CALCULATOR (self), 0);
    return self->snapshot_time;
}

void
lrg_idle_calculator_set_snapshot_time (LrgIdleCalculator *self,
                                       gint64             time)
{
    g_return_if_fail (LRG_IS_IDLE_CALCULATOR (self));

    if (self->snapshot_time != time)
    {
        self->snapshot_time = time;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SNAPSHOT_TIME]);
    }
}
