/* lrg-template-statistics.c - Game statistics tracking system
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#include "lrg-template-statistics.h"
#include "../save/lrg-saveable.h"
#include "../save/lrg-save-context.h"
#include "../lrg-log.h"

/**
 * SECTION:lrg-template-statistics
 * @title: LrgTemplateStatistics
 * @short_description: Game statistics tracking system
 *
 * See lrg-template-statistics.h for full documentation.
 */

/* Timer state structure */
typedef struct
{
    gdouble  accumulated;    /* Total accumulated time */
    gint64   start_time;     /* Start timestamp (usec) or 0 if stopped */
    gboolean running;        /* Whether timer is currently running */
} TimerState;

struct _LrgTemplateStatistics
{
    GObject      parent_instance;

    gchar       *save_id;

    /* Statistics storage - name -> value */
    GHashTable  *counters;   /* name -> gint64* */
    GHashTable  *maximums;   /* name -> gdouble* */
    GHashTable  *minimums;   /* name -> gdouble* */
    GHashTable  *timers;     /* name -> TimerState* */
};

static void lrg_saveable_iface_init (LrgSaveableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (LrgTemplateStatistics, lrg_template_statistics, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                lrg_saveable_iface_init))

enum
{
    PROP_0,
    PROP_SAVE_ID,
    N_PROPS
};

static GParamSpec *props[N_PROPS];

/* ==========================================================================
 * Helper Functions
 * ========================================================================== */

static gint64 *
int64_dup (gint64 value)
{
    gint64 *copy = g_new (gint64, 1);
    *copy = value;
    return copy;
}

static gdouble *
double_dup (gdouble value)
{
    gdouble *copy = g_new (gdouble, 1);
    *copy = value;
    return copy;
}

static TimerState *
timer_state_new (void)
{
    TimerState *state = g_new0 (TimerState, 1);
    state->accumulated = 0.0;
    state->start_time = 0;
    state->running = FALSE;
    return state;
}

static void
timer_state_free (TimerState *state)
{
    g_free (state);
}

static gdouble
timer_state_get_current (TimerState *state)
{
    gdouble total = state->accumulated;

    if (state->running)
    {
        gint64 now = g_get_monotonic_time ();
        gdouble elapsed = (gdouble) (now - state->start_time) / G_USEC_PER_SEC;
        total += elapsed;
    }

    return total;
}

/* ==========================================================================
 * LrgSaveable Interface Implementation
 * ========================================================================== */

static const gchar *
lrg_template_statistics_saveable_get_save_id (LrgSaveable *saveable)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (saveable);
    return self->save_id;
}

static gboolean
lrg_template_statistics_saveable_save (LrgSaveable     *saveable,
                                        LrgSaveContext  *context,
                                        GError         **error)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (saveable);
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    /* Save counters: store names as comma-separated, then each value */
    {
        GString *names = g_string_new (NULL);
        guint idx = 0;

        g_hash_table_iter_init (&iter, self->counters);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            g_autofree gchar *key_name = g_strdup_printf ("counter_v_%u", idx);
            lrg_save_context_write_int (context, key_name, *(gint64 *) value);

            if (names->len > 0)
                g_string_append_c (names, ',');
            g_string_append (names, (gchar *) key);
            idx++;
        }

        lrg_save_context_write_string (context, "counter_names", names->str);
        g_string_free (names, TRUE);
    }

    /* Save maximums */
    {
        GString *names = g_string_new (NULL);
        guint idx = 0;

        g_hash_table_iter_init (&iter, self->maximums);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            g_autofree gchar *key_name = g_strdup_printf ("maximum_v_%u", idx);
            lrg_save_context_write_double (context, key_name, *(gdouble *) value);

            if (names->len > 0)
                g_string_append_c (names, ',');
            g_string_append (names, (gchar *) key);
            idx++;
        }

        lrg_save_context_write_string (context, "maximum_names", names->str);
        g_string_free (names, TRUE);
    }

    /* Save minimums */
    {
        GString *names = g_string_new (NULL);
        guint idx = 0;

        g_hash_table_iter_init (&iter, self->minimums);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            g_autofree gchar *key_name = g_strdup_printf ("minimum_v_%u", idx);
            lrg_save_context_write_double (context, key_name, *(gdouble *) value);

            if (names->len > 0)
                g_string_append_c (names, ',');
            g_string_append (names, (gchar *) key);
            idx++;
        }

        lrg_save_context_write_string (context, "minimum_names", names->str);
        g_string_free (names, TRUE);
    }

    /* Save timers (accumulated time only, not running state) */
    {
        GString *names = g_string_new (NULL);
        guint idx = 0;

        g_hash_table_iter_init (&iter, self->timers);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            TimerState *timer = (TimerState *) value;
            g_autofree gchar *key_name = g_strdup_printf ("timer_v_%u", idx);
            lrg_save_context_write_double (context, key_name, timer_state_get_current (timer));

            if (names->len > 0)
                g_string_append_c (names, ',');
            g_string_append (names, (gchar *) key);
            idx++;
        }

        lrg_save_context_write_string (context, "timer_names", names->str);
        g_string_free (names, TRUE);
    }

    return TRUE;
}

static gboolean
lrg_template_statistics_saveable_load (LrgSaveable     *saveable,
                                        LrgSaveContext  *context,
                                        GError         **error)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (saveable);
    const gchar *names_str;
    gchar **names;
    guint idx;

    /* Clear existing data */
    g_hash_table_remove_all (self->counters);
    g_hash_table_remove_all (self->maximums);
    g_hash_table_remove_all (self->minimums);
    g_hash_table_remove_all (self->timers);

    /* Load counters */
    names_str = lrg_save_context_read_string (context, "counter_names", "");
    if (names_str != NULL && *names_str != '\0')
    {
        names = g_strsplit (names_str, ",", -1);
        for (idx = 0; names[idx] != NULL; idx++)
        {
            g_autofree gchar *key_name = g_strdup_printf ("counter_v_%u", idx);
            gint64 value = lrg_save_context_read_int (context, key_name, 0);
            lrg_template_statistics_set_counter (self, names[idx], value);
        }
        g_strfreev (names);
    }

    /* Load maximums */
    names_str = lrg_save_context_read_string (context, "maximum_names", "");
    if (names_str != NULL && *names_str != '\0')
    {
        names = g_strsplit (names_str, ",", -1);
        for (idx = 0; names[idx] != NULL; idx++)
        {
            g_autofree gchar *key_name = g_strdup_printf ("maximum_v_%u", idx);
            gdouble value = lrg_save_context_read_double (context, key_name, -G_MAXDOUBLE);
            if (value > -G_MAXDOUBLE)
                lrg_template_statistics_track_maximum (self, names[idx], value);
        }
        g_strfreev (names);
    }

    /* Load minimums */
    names_str = lrg_save_context_read_string (context, "minimum_names", "");
    if (names_str != NULL && *names_str != '\0')
    {
        names = g_strsplit (names_str, ",", -1);
        for (idx = 0; names[idx] != NULL; idx++)
        {
            g_autofree gchar *key_name = g_strdup_printf ("minimum_v_%u", idx);
            gdouble value = lrg_save_context_read_double (context, key_name, G_MAXDOUBLE);
            if (value < G_MAXDOUBLE)
                lrg_template_statistics_track_minimum (self, names[idx], value);
        }
        g_strfreev (names);
    }

    /* Load timers (as accumulated time, not running) */
    names_str = lrg_save_context_read_string (context, "timer_names", "");
    if (names_str != NULL && *names_str != '\0')
    {
        names = g_strsplit (names_str, ",", -1);
        for (idx = 0; names[idx] != NULL; idx++)
        {
            g_autofree gchar *key_name = g_strdup_printf ("timer_v_%u", idx);
            gdouble value = lrg_save_context_read_double (context, key_name, 0.0);

            /* Create timer with accumulated value */
            TimerState *timer = timer_state_new ();
            timer->accumulated = value;
            g_hash_table_insert (self->timers, g_strdup (names[idx]), timer);
        }
        g_strfreev (names);
    }

    return TRUE;
}

static void
lrg_saveable_iface_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = lrg_template_statistics_saveable_get_save_id;
    iface->save = lrg_template_statistics_saveable_save;
    iface->load = lrg_template_statistics_saveable_load;
}

/* ==========================================================================
 * GObject Virtual Methods
 * ========================================================================== */

static void
lrg_template_statistics_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (object);

    switch (prop_id)
    {
    case PROP_SAVE_ID:
        g_free (self->save_id);
        self->save_id = g_value_dup_string (value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_statistics_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (object);

    switch (prop_id)
    {
    case PROP_SAVE_ID:
        g_value_set_string (value, self->save_id);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_template_statistics_finalize (GObject *object)
{
    LrgTemplateStatistics *self = LRG_TEMPLATE_STATISTICS (object);

    g_free (self->save_id);
    g_hash_table_destroy (self->counters);
    g_hash_table_destroy (self->maximums);
    g_hash_table_destroy (self->minimums);
    g_hash_table_destroy (self->timers);

    G_OBJECT_CLASS (lrg_template_statistics_parent_class)->finalize (object);
}

static void
lrg_template_statistics_class_init (LrgTemplateStatisticsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = lrg_template_statistics_set_property;
    object_class->get_property = lrg_template_statistics_get_property;
    object_class->finalize = lrg_template_statistics_finalize;

    /**
     * LrgTemplateStatistics:save-id:
     *
     * The unique identifier for save/load operations.
     */
    props[PROP_SAVE_ID] =
        g_param_spec_string ("save-id",
                             "Save ID",
                             "Unique save identifier",
                             "statistics",
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, props);
}

static void
lrg_template_statistics_init (LrgTemplateStatistics *self)
{
    self->save_id = g_strdup ("statistics");

    self->counters = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_free);
    self->maximums = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_free);
    self->minimums = g_hash_table_new_full (g_str_hash, g_str_equal,
                                            g_free, g_free);
    self->timers = g_hash_table_new_full (g_str_hash, g_str_equal,
                                          g_free, (GDestroyNotify) timer_state_free);
}

/* ==========================================================================
 * Public API - Construction
 * ========================================================================== */

/**
 * lrg_template_statistics_new:
 * @save_id: unique identifier for save/load operations
 *
 * Creates a new statistics tracker.
 *
 * Returns: (transfer full): A new #LrgTemplateStatistics
 */
LrgTemplateStatistics *
lrg_template_statistics_new (const gchar *save_id)
{
    return g_object_new (LRG_TYPE_TEMPLATE_STATISTICS,
                         "save-id", save_id != NULL ? save_id : "statistics",
                         NULL);
}

/* ==========================================================================
 * Public API - Counters
 * ========================================================================== */

/**
 * lrg_template_statistics_track_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @increment: value to add
 *
 * Increments a counter statistic.
 */
void
lrg_template_statistics_track_counter (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gint64                 increment)
{
    gint64 *current;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    current = g_hash_table_lookup (self->counters, name);
    if (current != NULL)
    {
        *current += increment;
    }
    else
    {
        g_hash_table_insert (self->counters,
                            g_strdup (name),
                            int64_dup (increment));
    }
}

/**
 * lrg_template_statistics_get_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets a counter value.
 *
 * Returns: The counter value, or 0 if not found
 */
gint64
lrg_template_statistics_get_counter (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    gint64 *value;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    value = g_hash_table_lookup (self->counters, name);
    return (value != NULL) ? *value : 0;
}

/**
 * lrg_template_statistics_set_counter:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the new value
 *
 * Sets a counter to an absolute value.
 */
void
lrg_template_statistics_set_counter (LrgTemplateStatistics *self,
                                      const gchar           *name,
                                      gint64                 value)
{
    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    g_hash_table_insert (self->counters,
                        g_strdup (name),
                        int64_dup (value));
}

/* ==========================================================================
 * Public API - Maximums
 * ========================================================================== */

/**
 * lrg_template_statistics_track_maximum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the value to compare
 *
 * Updates a maximum statistic if value exceeds current.
 */
void
lrg_template_statistics_track_maximum (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gdouble                value)
{
    gdouble *current;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    current = g_hash_table_lookup (self->maximums, name);
    if (current != NULL)
    {
        if (value > *current)
            *current = value;
    }
    else
    {
        g_hash_table_insert (self->maximums,
                            g_strdup (name),
                            double_dup (value));
    }
}

/**
 * lrg_template_statistics_get_maximum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets a maximum value.
 *
 * Returns: The maximum value, or -G_MAXDOUBLE if not found
 */
gdouble
lrg_template_statistics_get_maximum (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    gdouble *value;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), -G_MAXDOUBLE);
    g_return_val_if_fail (name != NULL, -G_MAXDOUBLE);

    value = g_hash_table_lookup (self->maximums, name);
    return (value != NULL) ? *value : -G_MAXDOUBLE;
}

/* ==========================================================================
 * Public API - Minimums
 * ========================================================================== */

/**
 * lrg_template_statistics_track_minimum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 * @value: the value to compare
 *
 * Updates a minimum statistic if value is lower than current.
 */
void
lrg_template_statistics_track_minimum (LrgTemplateStatistics *self,
                                        const gchar           *name,
                                        gdouble                value)
{
    gdouble *current;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    current = g_hash_table_lookup (self->minimums, name);
    if (current != NULL)
    {
        if (value < *current)
            *current = value;
    }
    else
    {
        g_hash_table_insert (self->minimums,
                            g_strdup (name),
                            double_dup (value));
    }
}

/**
 * lrg_template_statistics_get_minimum:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Gets a minimum value.
 *
 * Returns: The minimum value, or G_MAXDOUBLE if not found
 */
gdouble
lrg_template_statistics_get_minimum (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    gdouble *value;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), G_MAXDOUBLE);
    g_return_val_if_fail (name != NULL, G_MAXDOUBLE);

    value = g_hash_table_lookup (self->minimums, name);
    return (value != NULL) ? *value : G_MAXDOUBLE;
}

/* ==========================================================================
 * Public API - Timers
 * ========================================================================== */

/**
 * lrg_template_statistics_timer_start:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Starts or resumes a timer.
 */
void
lrg_template_statistics_timer_start (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    TimerState *timer;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    timer = g_hash_table_lookup (self->timers, name);
    if (timer == NULL)
    {
        timer = timer_state_new ();
        g_hash_table_insert (self->timers, g_strdup (name), timer);
    }

    if (!timer->running)
    {
        timer->running = TRUE;
        timer->start_time = g_get_monotonic_time ();
    }
}

/**
 * lrg_template_statistics_timer_stop:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Stops a timer and accumulates elapsed time.
 */
void
lrg_template_statistics_timer_stop (LrgTemplateStatistics *self,
                                     const gchar           *name)
{
    TimerState *timer;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    timer = g_hash_table_lookup (self->timers, name);
    if (timer != NULL && timer->running)
    {
        gint64 now = g_get_monotonic_time ();
        gdouble elapsed = (gdouble) (now - timer->start_time) / G_USEC_PER_SEC;
        timer->accumulated += elapsed;
        timer->running = FALSE;
        timer->start_time = 0;
    }
}

/**
 * lrg_template_statistics_timer_reset:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Resets a timer to zero.
 */
void
lrg_template_statistics_timer_reset (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    TimerState *timer;

    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));
    g_return_if_fail (name != NULL);

    timer = g_hash_table_lookup (self->timers, name);
    if (timer != NULL)
    {
        timer->accumulated = 0.0;
        timer->running = FALSE;
        timer->start_time = 0;
    }
}

/**
 * lrg_template_statistics_get_timer:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Gets total accumulated time in seconds.
 *
 * Returns: The time in seconds, or 0.0 if not found
 */
gdouble
lrg_template_statistics_get_timer (LrgTemplateStatistics *self,
                                    const gchar           *name)
{
    TimerState *timer;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), 0.0);
    g_return_val_if_fail (name != NULL, 0.0);

    timer = g_hash_table_lookup (self->timers, name);
    if (timer == NULL)
        return 0.0;

    return timer_state_get_current (timer);
}

/**
 * lrg_template_statistics_is_timer_running:
 * @self: an #LrgTemplateStatistics
 * @name: the timer name
 *
 * Checks if a timer is running.
 *
 * Returns: %TRUE if running
 */
gboolean
lrg_template_statistics_is_timer_running (LrgTemplateStatistics *self,
                                           const gchar           *name)
{
    TimerState *timer;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    timer = g_hash_table_lookup (self->timers, name);
    return (timer != NULL) ? timer->running : FALSE;
}

/* ==========================================================================
 * Public API - Utility
 * ========================================================================== */

/**
 * lrg_template_statistics_has_stat:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Checks if a statistic exists.
 *
 * Returns: %TRUE if exists
 */
gboolean
lrg_template_statistics_has_stat (LrgTemplateStatistics *self,
                                   const gchar           *name)
{
    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->counters, name) ||
           g_hash_table_contains (self->maximums, name) ||
           g_hash_table_contains (self->minimums, name) ||
           g_hash_table_contains (self->timers, name);
}

/**
 * lrg_template_statistics_remove_stat:
 * @self: an #LrgTemplateStatistics
 * @name: the statistic name
 *
 * Removes a statistic.
 *
 * Returns: %TRUE if removed
 */
gboolean
lrg_template_statistics_remove_stat (LrgTemplateStatistics *self,
                                      const gchar           *name)
{
    gboolean removed = FALSE;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    removed |= g_hash_table_remove (self->counters, name);
    removed |= g_hash_table_remove (self->maximums, name);
    removed |= g_hash_table_remove (self->minimums, name);
    removed |= g_hash_table_remove (self->timers, name);

    return removed;
}

/**
 * lrg_template_statistics_clear_all:
 * @self: an #LrgTemplateStatistics
 *
 * Removes all statistics.
 */
void
lrg_template_statistics_clear_all (LrgTemplateStatistics *self)
{
    g_return_if_fail (LRG_IS_TEMPLATE_STATISTICS (self));

    g_hash_table_remove_all (self->counters);
    g_hash_table_remove_all (self->maximums);
    g_hash_table_remove_all (self->minimums);
    g_hash_table_remove_all (self->timers);
}

/**
 * lrg_template_statistics_get_all_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets all statistic names.
 *
 * Returns: (transfer full) (element-type utf8): List of names
 */
GList *
lrg_template_statistics_get_all_names (LrgTemplateStatistics *self)
{
    GList *names = NULL;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);

    g_hash_table_iter_init (&iter, self->counters);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    g_hash_table_iter_init (&iter, self->maximums);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    g_hash_table_iter_init (&iter, self->minimums);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    g_hash_table_iter_init (&iter, self->timers);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    return names;
}

/**
 * lrg_template_statistics_get_counter_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets all counter names.
 *
 * Returns: (transfer full) (element-type utf8): List of names
 */
GList *
lrg_template_statistics_get_counter_names (LrgTemplateStatistics *self)
{
    GList *names = NULL;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);

    g_hash_table_iter_init (&iter, self->counters);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    return names;
}

/**
 * lrg_template_statistics_get_maximum_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets all maximum stat names.
 *
 * Returns: (transfer full) (element-type utf8): List of names
 */
GList *
lrg_template_statistics_get_maximum_names (LrgTemplateStatistics *self)
{
    GList *names = NULL;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);

    g_hash_table_iter_init (&iter, self->maximums);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    return names;
}

/**
 * lrg_template_statistics_get_minimum_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets all minimum stat names.
 *
 * Returns: (transfer full) (element-type utf8): List of names
 */
GList *
lrg_template_statistics_get_minimum_names (LrgTemplateStatistics *self)
{
    GList *names = NULL;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);

    g_hash_table_iter_init (&iter, self->minimums);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    return names;
}

/**
 * lrg_template_statistics_get_timer_names:
 * @self: an #LrgTemplateStatistics
 *
 * Gets all timer names.
 *
 * Returns: (transfer full) (element-type utf8): List of names
 */
GList *
lrg_template_statistics_get_timer_names (LrgTemplateStatistics *self)
{
    GList *names = NULL;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);

    g_hash_table_iter_init (&iter, self->timers);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        names = g_list_prepend (names, g_strdup (key));

    return names;
}

/**
 * lrg_template_statistics_get_id:
 * @self: an #LrgTemplateStatistics
 *
 * Gets the save identifier.
 *
 * Returns: (transfer none): The save ID
 */
const gchar *
lrg_template_statistics_get_id (LrgTemplateStatistics *self)
{
    g_return_val_if_fail (LRG_IS_TEMPLATE_STATISTICS (self), NULL);
    return self->save_id;
}
