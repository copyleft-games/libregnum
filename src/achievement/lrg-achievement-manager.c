/* lrg-achievement-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementManager - Singleton manager for achievements.
 */

#include "config.h"

#include "lrg-achievement-manager.h"
#include <gio/gio.h>
#include "../save/lrg-saveable.h"
#include "../save/lrg-save-context.h"
#include "../lrg-log.h"

static void lrg_saveable_interface_init (LrgSaveableInterface *iface);

struct _LrgAchievementManager
{
    GObject parent_instance;

    GHashTable *achievements;     /* id -> LrgAchievement */
    GHashTable *stats_int;        /* name -> gint64 */
    GHashTable *stats_float;      /* name -> gdouble */
    GPtrArray  *order;            /* LrgAchievement (unowned), registration order */
    GHashTable *pending[2];       /* per scope: id -> GVariant (bxxa{s(xas)}) */
};

enum
{
    SIGNAL_ACHIEVEMENT_UNLOCKED,
    SIGNAL_ACHIEVEMENT_PROGRESS,
    SIGNAL_CRITERION_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];
static LrgAchievementManager *default_manager = NULL;

G_DEFINE_TYPE_WITH_CODE (LrgAchievementManager, lrg_achievement_manager, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (LRG_TYPE_SAVEABLE,
                                                lrg_saveable_interface_init))

/* ==========================================================================
 * LrgSaveable Interface Implementation
 * ========================================================================== */

static const gchar *
lrg_achievement_manager_get_save_id (LrgSaveable *saveable)
{
    (void)saveable;
    return "achievements";
}

static gboolean
lrg_achievement_manager_save (LrgSaveable      *saveable,
                              LrgSaveContext   *context,
                              GError          **error)
{
    LrgAchievementManager *self = LRG_ACHIEVEMENT_MANAGER (saveable);
    GHashTableIter iter;
    gpointer key, value;

    /* Save achievements */
    lrg_save_context_begin_section (context, "achievements");

    g_hash_table_iter_init (&iter, self->achievements);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        LrgAchievement *achievement = LRG_ACHIEVEMENT (value);
        const gchar *id = (const gchar *)key;

        if (lrg_achievement_is_unlocked (achievement))
        {
            GDateTime *unlock_time = lrg_achievement_get_unlock_time (achievement);
            g_autofree gchar *time_str = NULL;

            if (unlock_time != NULL)
                time_str = g_date_time_format_iso8601 (unlock_time);

            lrg_save_context_begin_section (context, id);
            lrg_save_context_write_boolean (context, "unlocked", TRUE);
            if (time_str != NULL)
                lrg_save_context_write_string (context, "unlock_time", time_str);

            /* Save progress if applicable */
            if (lrg_achievement_has_progress (achievement))
            {
                LrgAchievementProgress *progress = lrg_achievement_get_progress (achievement);
                lrg_save_context_write_int (context, "progress",
                                              lrg_achievement_progress_get_current (progress));
            }

            lrg_save_context_end_section (context);
        }
        else if (lrg_achievement_has_progress (achievement))
        {
            /* Save progress even if not unlocked */
            LrgAchievementProgress *progress = lrg_achievement_get_progress (achievement);
            gint64 current = lrg_achievement_progress_get_current (progress);

            if (current > 0)
            {
                lrg_save_context_begin_section (context, id);
                lrg_save_context_write_boolean (context, "unlocked", FALSE);
                lrg_save_context_write_int (context, "progress", current);
                lrg_save_context_end_section (context);
            }
        }
    }

    lrg_save_context_end_section (context);

    /* Save integer stats */
    lrg_save_context_begin_section (context, "stats_int");
    g_hash_table_iter_init (&iter, self->stats_int);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *name = (const gchar *)key;
        gint64 val = GPOINTER_TO_SIZE (value);
        lrg_save_context_write_int (context, name, val);
    }
    lrg_save_context_end_section (context);

    /* Save float stats */
    lrg_save_context_begin_section (context, "stats_float");
    g_hash_table_iter_init (&iter, self->stats_float);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *name = (const gchar *)key;
        gdouble *val_ptr = (gdouble *)value;
        lrg_save_context_write_double (context, name, *val_ptr);
    }
    lrg_save_context_end_section (context);

    return TRUE;
}

static gboolean
lrg_achievement_manager_load (LrgSaveable      *saveable,
                              LrgSaveContext   *context,
                              GError          **error)
{
    LrgAchievementManager *self = LRG_ACHIEVEMENT_MANAGER (saveable);
    GHashTableIter iter;
    gpointer key, value;

    /* Load achievements */
    if (lrg_save_context_has_section (context, "achievements"))
    {
        lrg_save_context_enter_section (context, "achievements");

        g_hash_table_iter_init (&iter, self->achievements);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            LrgAchievement *achievement = LRG_ACHIEVEMENT (value);
            const gchar *id = (const gchar *)key;

            if (lrg_save_context_has_section (context, id))
            {
                gboolean unlocked;
                gint64 progress;

                lrg_save_context_enter_section (context, id);

                unlocked = lrg_save_context_read_boolean (context, "unlocked", FALSE);
                progress = lrg_save_context_read_int (context, "progress", 0);

                if (lrg_achievement_has_progress (achievement))
                    lrg_achievement_set_progress_value (achievement, progress);

                if (unlocked && !lrg_achievement_is_unlocked (achievement))
                    lrg_achievement_unlock (achievement);

                lrg_save_context_leave_section (context);
            }
        }

        lrg_save_context_leave_section (context);
    }

    /* Load integer stats */
    if (lrg_save_context_has_section (context, "stats_int"))
    {
        /* Stats loading would need iteration over saved keys */
        /* For now, we'll load stats by their registered names */
    }

    return TRUE;
}

static void
lrg_saveable_interface_init (LrgSaveableInterface *iface)
{
    iface->get_save_id = lrg_achievement_manager_get_save_id;
    iface->save = lrg_achievement_manager_save;
    iface->load = lrg_achievement_manager_load;
}

/* ==========================================================================
 * Signal Handlers
 * ========================================================================== */

static void
on_achievement_unlocked (LrgAchievement        *achievement,
                         LrgAchievementManager *self)
{
    g_signal_emit (self, signals[SIGNAL_ACHIEVEMENT_UNLOCKED], 0, achievement);
}

static void
on_achievement_progress (LrgAchievement        *achievement,
                         gint64                 current,
                         gint64                 target,
                         LrgAchievementManager *self)
{
    g_signal_emit (self, signals[SIGNAL_ACHIEVEMENT_PROGRESS], 0,
                   achievement, current, target);
}

static void
on_criterion_changed (LrgAchievement        *achievement,
                      const gchar           *criterion,
                      gint64                 current,
                      gint64                 target,
                      LrgAchievementManager *self)
{
    g_signal_emit (self, signals[SIGNAL_CRITERION_CHANGED], 0,
                   achievement, criterion, current, target);
}

/* ==========================================================================
 * GObject Implementation
 * ========================================================================== */

static void
lrg_achievement_manager_finalize (GObject *object)
{
    LrgAchievementManager *self = LRG_ACHIEVEMENT_MANAGER (object);

    g_clear_pointer (&self->achievements, g_hash_table_unref);
    g_clear_pointer (&self->stats_int, g_hash_table_unref);
    g_clear_pointer (&self->stats_float, g_hash_table_unref);
    g_clear_pointer (&self->order, g_ptr_array_unref);
    g_clear_pointer (&self->pending[0], g_hash_table_unref);
    g_clear_pointer (&self->pending[1], g_hash_table_unref);

    G_OBJECT_CLASS (lrg_achievement_manager_parent_class)->finalize (object);
}

static void
lrg_achievement_manager_class_init (LrgAchievementManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_achievement_manager_finalize;

    /**
     * LrgAchievementManager::achievement-unlocked:
     * @self: the manager
     * @achievement: the unlocked achievement
     *
     * Emitted when any achievement is unlocked.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ACHIEVEMENT_UNLOCKED] =
        g_signal_new ("achievement-unlocked",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_ACHIEVEMENT);

    /**
     * LrgAchievementManager::achievement-progress:
     * @self: the manager
     * @achievement: the achievement
     * @current: current progress
     * @target: target progress
     *
     * Emitted when achievement progress changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_ACHIEVEMENT_PROGRESS] =
        g_signal_new ("achievement-progress",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 3,
                      LRG_TYPE_ACHIEVEMENT, G_TYPE_INT64, G_TYPE_INT64);

    /**
     * LrgAchievementManager::criterion-changed:
     * @self: the manager
     * @achievement: the achievement
     * @criterion: the criterion's id
     * @current: its progress
     * @target: its target
     *
     * Emitted when a criterion of any achievement moved.
     *
     * Since: 1.0
     */
    signals[SIGNAL_CRITERION_CHANGED] =
        g_signal_new ("criterion-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 4,
                      LRG_TYPE_ACHIEVEMENT, G_TYPE_STRING, G_TYPE_INT64, G_TYPE_INT64);
}

static void
free_double (gpointer data)
{
    g_slice_free (gdouble, data);
}

static void
lrg_achievement_manager_init (LrgAchievementManager *self)
{
    self->achievements = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                g_free, g_object_unref);
    self->stats_int = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, NULL);
    self->stats_float = g_hash_table_new_full (g_str_hash, g_str_equal,
                                               g_free, free_double);
    self->order = g_ptr_array_new ();
    self->pending[0] = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                              (GDestroyNotify)g_variant_unref);
    self->pending[1] = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                              (GDestroyNotify)g_variant_unref);
}

/* applies one saved entry (bxxa{s(xas)}) to an achievement, silently */
static void
apply_entry (LrgAchievement *achievement,
             GVariant       *entry)
{
    g_autoptr(GVariantIter) criteria = NULL;
    g_autoptr(GDateTime) when = NULL;
    const gchar *criterion_id;
    GVariant *criterion_value;
    gboolean unlocked;
    gint64 unix_time;
    gint64 progress;

    g_variant_get (entry, "(bxxa{s(xas)})", &unlocked, &unix_time, &progress, &criteria);
    if (lrg_achievement_has_progress (achievement))
        lrg_achievement_progress_set_current (lrg_achievement_get_progress (achievement), progress);
    while (g_variant_iter_loop (criteria, "{s@(xas)}", &criterion_id, &criterion_value))
    {
        LrgAchievementCriterion *criterion;
        g_autoptr(GVariantIter) keys = NULL;
        const gchar *key;
        gint64 current;

        criterion = lrg_achievement_get_criterion (achievement, criterion_id);
        if (criterion == NULL)
            continue;
        g_variant_get (criterion_value, "(xas)", &current, &keys);
        lrg_achievement_criterion_reset (criterion);
        while (g_variant_iter_loop (keys, "&s", &key))
            lrg_achievement_criterion_add_key (criterion, key);
        /* the count wins over the keys (a counted criterion has none) */
        lrg_achievement_criterion_set_current (criterion,
            MAX (current, lrg_achievement_criterion_get_current (criterion)));
    }
    if (unlocked && unix_time > 0)
        when = g_date_time_new_from_unix_utc (unix_time);
    lrg_achievement_restore (achievement, unlocked, when);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

LrgAchievementManager *
lrg_achievement_manager_get_default (void)
{
    if (default_manager == NULL)
        default_manager = g_object_new (LRG_TYPE_ACHIEVEMENT_MANAGER, NULL);

    return default_manager;
}

void
lrg_achievement_manager_register (LrgAchievementManager *self,
                                  LrgAchievement        *achievement)
{
    const gchar *id;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (LRG_IS_ACHIEVEMENT (achievement));

    id = lrg_achievement_get_id (achievement);
    g_return_if_fail (id != NULL);

    if (g_hash_table_contains (self->achievements, id))
    {
        lrg_warning (LRG_LOG_DOMAIN_ACHIEVEMENT,
                         "Achievement already registered: %s", id);
        g_object_unref (achievement);
        return;
    }

    /* Connect to signals */
    g_signal_connect (achievement, "unlocked",
                      G_CALLBACK (on_achievement_unlocked), self);
    g_signal_connect (achievement, "progress-changed",
                      G_CALLBACK (on_achievement_progress), self);
    g_signal_connect (achievement, "criterion-changed",
                      G_CALLBACK (on_criterion_changed), self);

    g_hash_table_insert (self->achievements, g_strdup (id), achievement);
    g_ptr_array_add (self->order, achievement);

    /* state loaded before it registered */
    {
        GHashTable *pending = self->pending[lrg_achievement_get_scope (achievement) ==
                                            LRG_ACHIEVEMENT_SCOPE_CHARACTER ? 1 : 0];
        GVariant *entry = g_hash_table_lookup (pending, id);

        if (entry != NULL)
        {
            apply_entry (achievement, entry);
            g_hash_table_remove (pending, id);
        }
    }

    lrg_debug (LRG_LOG_DOMAIN_ACHIEVEMENT,
                   "Registered achievement: %s", id);
}

gboolean
lrg_achievement_manager_unregister (LrgAchievementManager *self,
                                    const gchar           *id)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    {
        LrgAchievement *achievement = g_hash_table_lookup (self->achievements, id);

        if (achievement == NULL)
            return FALSE;
        g_signal_handlers_disconnect_by_data (achievement, self);
        g_ptr_array_remove (self->order, achievement);
    }
    return g_hash_table_remove (self->achievements, id);
}

LrgAchievement *
lrg_achievement_manager_get (LrgAchievementManager *self,
                             const gchar           *id)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), NULL);
    g_return_val_if_fail (id != NULL, NULL);

    return g_hash_table_lookup (self->achievements, id);
}

GList *
lrg_achievement_manager_get_all (LrgAchievementManager *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), NULL);

    return g_hash_table_get_values (self->achievements);
}

guint
lrg_achievement_manager_get_count (LrgAchievementManager *self)
{
    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);

    return g_hash_table_size (self->achievements);
}

gboolean
lrg_achievement_manager_unlock (LrgAchievementManager *self,
                                const gchar           *id)
{
    LrgAchievement *achievement;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    achievement = lrg_achievement_manager_get (self, id);
    if (achievement == NULL)
        return FALSE;

    return lrg_achievement_unlock (achievement);
}

void
lrg_achievement_manager_increment_progress (LrgAchievementManager *self,
                                            const gchar           *id,
                                            gint64                 amount)
{
    LrgAchievement *achievement;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (id != NULL);

    achievement = lrg_achievement_manager_get (self, id);
    if (achievement == NULL)
        return;

    lrg_achievement_increment_progress (achievement, amount);
}

void
lrg_achievement_manager_set_progress (LrgAchievementManager *self,
                                      const gchar           *id,
                                      gint64                 value)
{
    LrgAchievement *achievement;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (id != NULL);

    achievement = lrg_achievement_manager_get (self, id);
    if (achievement == NULL)
        return;

    lrg_achievement_set_progress_value (achievement, value);
}

gboolean
lrg_achievement_manager_is_unlocked (LrgAchievementManager *self,
                                     const gchar           *id)
{
    LrgAchievement *achievement;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), FALSE);
    g_return_val_if_fail (id != NULL, FALSE);

    achievement = lrg_achievement_manager_get (self, id);
    if (achievement == NULL)
        return FALSE;

    return lrg_achievement_is_unlocked (achievement);
}

guint
lrg_achievement_manager_get_unlocked_count (LrgAchievementManager *self)
{
    GHashTableIter iter;
    gpointer value;
    guint count = 0;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);

    g_hash_table_iter_init (&iter, self->achievements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgAchievement *achievement = LRG_ACHIEVEMENT (value);
        if (lrg_achievement_is_unlocked (achievement))
            count++;
    }

    return count;
}

guint
lrg_achievement_manager_get_total_points (LrgAchievementManager *self)
{
    GHashTableIter iter;
    gpointer value;
    guint total = 0;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);

    g_hash_table_iter_init (&iter, self->achievements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgAchievement *achievement = LRG_ACHIEVEMENT (value);
        total += lrg_achievement_get_points (achievement);
    }

    return total;
}

guint
lrg_achievement_manager_get_earned_points (LrgAchievementManager *self)
{
    GHashTableIter iter;
    gpointer value;
    guint earned = 0;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);

    g_hash_table_iter_init (&iter, self->achievements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgAchievement *achievement = LRG_ACHIEVEMENT (value);
        if (lrg_achievement_is_unlocked (achievement))
            earned += lrg_achievement_get_points (achievement);
    }

    return earned;
}

gdouble
lrg_achievement_manager_get_completion_percentage (LrgAchievementManager *self)
{
    guint total;
    guint unlocked;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0.0);

    total = lrg_achievement_manager_get_count (self);
    if (total == 0)
        return 0.0;

    unlocked = lrg_achievement_manager_get_unlocked_count (self);

    return (gdouble)unlocked / (gdouble)total;
}

void
lrg_achievement_manager_set_stat_int (LrgAchievementManager *self,
                                      const gchar           *name,
                                      gint64                 value)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (name != NULL);

    g_hash_table_insert (self->stats_int, g_strdup (name),
                         GSIZE_TO_POINTER ((gsize)value));
}

gint64
lrg_achievement_manager_get_stat_int (LrgAchievementManager *self,
                                      const gchar           *name)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);
    g_return_val_if_fail (name != NULL, 0);

    if (!g_hash_table_lookup_extended (self->stats_int, name, NULL, &value))
        return 0;

    return (gint64)GPOINTER_TO_SIZE (value);
}

void
lrg_achievement_manager_increment_stat (LrgAchievementManager *self,
                                        const gchar           *name,
                                        gint64                 amount)
{
    gint64 current;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (name != NULL);

    current = lrg_achievement_manager_get_stat_int (self, name);
    lrg_achievement_manager_set_stat_int (self, name, current + amount);
}

void
lrg_achievement_manager_set_stat_float (LrgAchievementManager *self,
                                        const gchar           *name,
                                        gdouble                value)
{
    gdouble *val_ptr;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));
    g_return_if_fail (name != NULL);

    val_ptr = g_slice_new (gdouble);
    *val_ptr = value;

    g_hash_table_insert (self->stats_float, g_strdup (name), val_ptr);
}

gdouble
lrg_achievement_manager_get_stat_float (LrgAchievementManager *self,
                                        const gchar           *name)
{
    gdouble *val_ptr;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0.0);
    g_return_val_if_fail (name != NULL, 0.0);

    val_ptr = g_hash_table_lookup (self->stats_float, name);
    if (val_ptr == NULL)
        return 0.0;

    return *val_ptr;
}

void
lrg_achievement_manager_reset_all (LrgAchievementManager *self)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));

    g_hash_table_iter_init (&iter, self->achievements);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgAchievement *achievement = LRG_ACHIEVEMENT (value);

        lrg_achievement_lock (achievement);

        if (lrg_achievement_has_progress (achievement))
        {
            LrgAchievementProgress *progress = lrg_achievement_get_progress (achievement);
            lrg_achievement_progress_reset (progress);
        }
    }

    lrg_info (LRG_LOG_DOMAIN_ACHIEVEMENT, "All achievements reset");
}

void
lrg_achievement_manager_reset_stats (LrgAchievementManager *self)
{
    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));

    g_hash_table_remove_all (self->stats_int);
    g_hash_table_remove_all (self->stats_float);

    lrg_info (LRG_LOG_DOMAIN_ACHIEVEMENT, "All statistics reset");
}

/* ==========================================================================
 * Instances, listing, scopes and state
 * ========================================================================== */

LrgAchievementManager *
lrg_achievement_manager_new (void)
{
    return g_object_new (LRG_TYPE_ACHIEVEMENT_MANAGER, NULL);
}

GPtrArray *
lrg_achievement_manager_list (LrgAchievementManager *self,
                              const gchar           *category)
{
    GPtrArray *list;
    guint i;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), NULL);

    list = g_ptr_array_new ();
    for (i = 0; i < self->order->len; i++)
    {
        LrgAchievement *achievement = g_ptr_array_index (self->order, i);

        if (category == NULL || g_strcmp0 (lrg_achievement_get_category (achievement), category) == 0)
            g_ptr_array_add (list, achievement);
    }
    return list;
}

gchar **
lrg_achievement_manager_get_categories (LrgAchievementManager *self)
{
    GPtrArray *names;
    guint i;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), NULL);

    names = g_ptr_array_new ();
    for (i = 0; i < self->order->len; i++)
    {
        const gchar *category = lrg_achievement_get_category (g_ptr_array_index (self->order, i));
        guint k;
        gboolean seen = FALSE;

        if (category == NULL)
            continue;
        for (k = 0; k < names->len && !seen; k++)
            seen = g_strcmp0 (g_ptr_array_index (names, k), category) == 0;
        if (!seen)
            g_ptr_array_add (names, g_strdup (category));
    }
    g_ptr_array_add (names, NULL);
    return (gchar **)g_ptr_array_free (names, FALSE);
}

guint
lrg_achievement_manager_count_points (LrgAchievementManager *self,
                                      const gchar           *category,
                                      guint                 *earned,
                                      guint                 *total)
{
    guint unlocked = 0;
    guint e = 0;
    guint t = 0;
    guint i;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), 0);

    for (i = 0; i < self->order->len; i++)
    {
        LrgAchievement *achievement = g_ptr_array_index (self->order, i);

        if (category != NULL && g_strcmp0 (lrg_achievement_get_category (achievement), category) != 0)
            continue;
        t += lrg_achievement_get_points (achievement);
        if (lrg_achievement_is_unlocked (achievement))
        {
            e += lrg_achievement_get_points (achievement);
            unlocked++;
        }
    }
    if (earned != NULL)
        *earned = e;
    if (total != NULL)
        *total = t;
    return unlocked;
}

/* one achievement's entry, NULL when it has nothing to keep */
static GVariant *
entry_for (LrgAchievement *achievement)
{
    GVariantBuilder criteria;
    GPtrArray *list;
    GDateTime *when;
    gboolean any;
    gint64 progress;
    guint i;

    any = lrg_achievement_is_unlocked (achievement);
    progress = 0;
    if (lrg_achievement_has_progress (achievement))
        progress = lrg_achievement_progress_get_current (lrg_achievement_get_progress (achievement));
    any = any || progress > 0;
    g_variant_builder_init (&criteria, G_VARIANT_TYPE ("a{s(xas)}"));
    list = lrg_achievement_get_criteria (achievement);
    for (i = 0; i < list->len; i++)
    {
        LrgAchievementCriterion *criterion = g_ptr_array_index (list, i);
        g_auto(GStrv) keys = lrg_achievement_criterion_get_keys (criterion);
        gint64 current = lrg_achievement_criterion_get_current (criterion);

        if (current == 0 && keys[0] == NULL)
            continue;
        any = TRUE;
        g_variant_builder_add (&criteria, "{s(x^as)}", lrg_achievement_criterion_get_id (criterion),
                               current, keys);
    }
    if (!any)
    {
        g_variant_builder_clear (&criteria);
        return NULL;
    }
    when = lrg_achievement_get_unlock_time (achievement);
    return g_variant_new ("(bxxa{s(xas)})", lrg_achievement_is_unlocked (achievement),
                          lrg_achievement_is_unlocked (achievement) && when != NULL ?
                              g_date_time_to_unix (when) : (gint64)0,
                          progress, &criteria);
}

GVariant *
lrg_achievement_manager_save_state (LrgAchievementManager *self,
                                    LrgAchievementScope    scope)
{
    GVariantBuilder builder;
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    guint i;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), NULL);

    g_variant_builder_init (&builder, G_VARIANT_TYPE (LRG_ACHIEVEMENT_STATE_TYPE));
    for (i = 0; i < self->order->len; i++)
    {
        LrgAchievement *achievement = g_ptr_array_index (self->order, i);
        GVariant *entry;

        if (lrg_achievement_get_scope (achievement) != scope)
            continue;
        entry = entry_for (achievement);
        if (entry != NULL)
            g_variant_builder_add (&builder, "{s@(bxxa{s(xas)})}", lrg_achievement_get_id (achievement), entry);
    }
    /* what waits for achievements not registered keeps its place */
    g_hash_table_iter_init (&iter, self->pending[scope == LRG_ACHIEVEMENT_SCOPE_CHARACTER ? 1 : 0]);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_variant_builder_add (&builder, "{s@(bxxa{s(xas)})}", (const gchar *)key, (GVariant *)value);
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
lrg_achievement_manager_load_state (LrgAchievementManager  *self,
                                    LrgAchievementScope     scope,
                                    GVariant               *state,
                                    GError                **error)
{
    GVariantIter iter;
    const gchar *id;
    GVariant *entry;

    g_return_val_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self), FALSE);
    g_return_val_if_fail (state != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!g_variant_is_of_type (state, G_VARIANT_TYPE (LRG_ACHIEVEMENT_STATE_TYPE)))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                     "achievement state of type %s, expected %s",
                     g_variant_get_type_string (state), LRG_ACHIEVEMENT_STATE_TYPE);
        return FALSE;
    }
    lrg_achievement_manager_reset_scope (self, scope);
    g_variant_iter_init (&iter, state);
    while (g_variant_iter_loop (&iter, "{&s@(bxxa{s(xas)})}", &id, &entry))
    {
        LrgAchievement *achievement = g_hash_table_lookup (self->achievements, id);

        if (achievement == NULL)
        {
            g_hash_table_replace (self->pending[scope == LRG_ACHIEVEMENT_SCOPE_CHARACTER ? 1 : 0],
                                  g_strdup (id), g_variant_ref (entry));
            continue;
        }
        if (lrg_achievement_get_scope (achievement) != scope)
            continue;
        apply_entry (achievement, entry);
    }
    return TRUE;
}

void
lrg_achievement_manager_reset_scope (LrgAchievementManager *self,
                                     LrgAchievementScope    scope)
{
    guint i;

    g_return_if_fail (LRG_IS_ACHIEVEMENT_MANAGER (self));

    for (i = 0; i < self->order->len; i++)
    {
        LrgAchievement *achievement = g_ptr_array_index (self->order, i);

        if (lrg_achievement_get_scope (achievement) == scope)
            lrg_achievement_reset (achievement);
    }
    g_hash_table_remove_all (self->pending[scope == LRG_ACHIEVEMENT_SCOPE_CHARACTER ? 1 : 0]);
}
