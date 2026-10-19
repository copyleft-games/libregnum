/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lrg-mmo-lockout.h"
#include "lrg-mmo-service-private.h"

#define NAME_LIMIT (128)

struct _LrgMmoLockout
{
    GObject parent_instance;
    LrgResetSchedule *schedule;
    LrgResetPeriod period;
    GHashTable *locks;      /* "account\x1fkey" -> gint64 period index (boxed) */
};

G_DEFINE_FINAL_TYPE (LrgMmoLockout, lrg_mmo_lockout, G_TYPE_OBJECT)

static void
lrg_mmo_lockout_finalize (GObject *object)
{
    LrgMmoLockout *self = LRG_MMO_LOCKOUT (object);

    g_clear_pointer (&self->locks, g_hash_table_unref);
    g_clear_object (&self->schedule);
    G_OBJECT_CLASS (lrg_mmo_lockout_parent_class)->finalize (object);
}

static void
lrg_mmo_lockout_class_init (LrgMmoLockoutClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = lrg_mmo_lockout_finalize;
}

static void
lrg_mmo_lockout_init (LrgMmoLockout *self)
{
    self->locks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

LrgMmoLockout *
lrg_mmo_lockout_new (LrgResetSchedule *schedule,
                     LrgResetPeriod    period)
{
    LrgMmoLockout *self;

    g_return_val_if_fail (schedule == NULL || LRG_IS_RESET_SCHEDULE (schedule), NULL);
    self = g_object_new (LRG_TYPE_MMO_LOCKOUT, NULL);
    self->schedule = schedule != NULL ? g_object_ref (schedule) : lrg_reset_schedule_new (0, 2, 0);
    self->period = period;
    return self;
}

LrgResetSchedule *
lrg_mmo_lockout_get_schedule (LrgMmoLockout *self)
{
    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), NULL);
    return self->schedule;
}

LrgResetPeriod
lrg_mmo_lockout_get_period (LrgMmoLockout *self)
{
    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), LRG_RESET_PERIOD_WEEKLY);
    return self->period;
}

static gboolean
name_ok (const gchar *name)
{
    gsize len;

    if (name == NULL)
        return FALSE;
    len = strlen (name);
    /* the unit separator joins the two names inside the table's key */
    return len > 0 && len <= NAME_LIMIT && strchr (name, '\x1f') == NULL &&
           g_utf8_validate (name, -1, NULL);
}

static gchar *
make_key (const gchar *account,
          const gchar *key)
{
    return g_strdup_printf ("%s\x1f%s", account, key);
}

gboolean
lrg_mmo_lockout_is_locked (LrgMmoLockout *self,
                           const gchar   *account,
                           const gchar   *key,
                           gint64         now)
{
    g_autofree gchar *joined = NULL;
    gint64 *index;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), FALSE);
    if (!name_ok (account) || !name_ok (key))
        return FALSE;
    joined = make_key (account, key);
    index = g_hash_table_lookup (self->locks, joined);
    return index != NULL && *index == lrg_reset_schedule_get_period (self->schedule, self->period, now);
}

gboolean
lrg_mmo_lockout_lock (LrgMmoLockout *self,
                      const gchar   *account,
                      const gchar   *key,
                      gint64         now)
{
    gchar *joined;
    gint64 *index;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), FALSE);
    if (!name_ok (account) || !name_ok (key))
        return FALSE;
    if (lrg_mmo_lockout_is_locked (self, account, key, now))
        return FALSE;
    joined = make_key (account, key);
    index = g_new (gint64, 1);
    *index = lrg_reset_schedule_get_period (self->schedule, self->period, now);
    g_hash_table_replace (self->locks, joined, index);
    return TRUE;
}

gboolean
lrg_mmo_lockout_unlock (LrgMmoLockout *self,
                        const gchar   *account,
                        const gchar   *key)
{
    g_autofree gchar *joined = NULL;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), FALSE);
    if (!name_ok (account) || !name_ok (key))
        return FALSE;
    joined = make_key (account, key);
    return g_hash_table_remove (self->locks, joined);
}

gint64
lrg_mmo_lockout_get_reset (LrgMmoLockout *self,
                           const gchar   *account,
                           const gchar   *key,
                           gint64         now)
{
    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), 0);
    if (!lrg_mmo_lockout_is_locked (self, account, key, now))
        return 0;
    return lrg_reset_schedule_get_next_reset (self->schedule, self->period, now);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

GVariant *
lrg_mmo_lockout_list (LrgMmoLockout *self,
                      const gchar   *account,
                      gint64         now)
{
    GVariantBuilder builder;
    g_autoptr(GPtrArray) keys = g_ptr_array_new_with_free_func (g_free);
    GHashTableIter iter;
    gpointer name;
    gpointer value;
    g_autofree gchar *prefix = NULL;
    gint64 current;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), NULL);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sx)"));
    if (name_ok (account))
    {
        prefix = g_strdup_printf ("%s\x1f", account);
        current = lrg_reset_schedule_get_period (self->schedule, self->period, now);
        g_hash_table_iter_init (&iter, self->locks);
        while (g_hash_table_iter_next (&iter, &name, &value))
            if (g_str_has_prefix (name, prefix) && *(gint64 *) value == current)
                g_ptr_array_add (keys, g_strdup ((const gchar *) name + strlen (prefix)));
        g_ptr_array_sort (keys, compare_strings);
        for (i = 0; i < keys->len; i++)
            g_variant_builder_add (&builder, "(sx)", (const gchar *) g_ptr_array_index (keys, i),
                                   lrg_reset_schedule_get_next_reset (self->schedule, self->period, now));
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

guint
lrg_mmo_lockout_purge (LrgMmoLockout *self,
                       gint64         now)
{
    GHashTableIter iter;
    gpointer value;
    gint64 current;
    guint dropped = 0;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), 0);
    current = lrg_reset_schedule_get_period (self->schedule, self->period, now);
    g_hash_table_iter_init (&iter, self->locks);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        if (*(gint64 *) value != current)
        {
            g_hash_table_iter_remove (&iter);
            dropped++;
        }
    return dropped;
}

guint
lrg_mmo_lockout_get_count (LrgMmoLockout *self)
{
    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), 0);
    return g_hash_table_size (self->locks);
}

void
lrg_mmo_lockout_clear (LrgMmoLockout *self)
{
    g_return_if_fail (LRG_IS_MMO_LOCKOUT (self));
    g_hash_table_remove_all (self->locks);
}

GVariant *
lrg_mmo_lockout_to_variant (LrgMmoLockout *self)
{
    GVariantBuilder builder;
    g_autoptr(GPtrArray) keys = g_ptr_array_new ();
    GHashTableIter iter;
    gpointer name;
    guint i;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), NULL);
    g_hash_table_iter_init (&iter, self->locks);
    while (g_hash_table_iter_next (&iter, &name, NULL))
        g_ptr_array_add (keys, name);
    g_ptr_array_sort (keys, compare_strings);
    g_variant_builder_init (&builder, G_VARIANT_TYPE (LRG_MMO_LOCKOUT_VARIANT_TYPE));
    for (i = 0; i < keys->len; i++)
    {
        const gchar *joined = g_ptr_array_index (keys, i);
        const gchar *split = strchr (joined, '\x1f');
        g_autofree gchar *account = g_strndup (joined, (gsize) (split - joined));

        g_variant_builder_add (&builder, "(ssx)", account, split + 1,
                               *(gint64 *) g_hash_table_lookup (self->locks, joined));
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

gboolean
lrg_mmo_lockout_restore (LrgMmoLockout *self,
                         GVariant      *state,
                         GError       **error)
{
    g_autoptr(GHashTable) staged = NULL;
    GVariantIter iter;
    const gchar *account;
    const gchar *key;
    gint64 index;

    g_return_val_if_fail (LRG_IS_MMO_LOCKOUT (self), FALSE);
    if (state == NULL)
    {
        g_hash_table_remove_all (self->locks);
        return TRUE;
    }
    if (!g_variant_is_of_type (state, G_VARIANT_TYPE (LRG_MMO_LOCKOUT_VARIANT_TYPE)))
        return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_ARGUMENT, "Lockout state has the wrong type");
    /* staged first: a bad entry leaves the live book untouched */
    staged = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    g_variant_iter_init (&iter, state);
    while (g_variant_iter_next (&iter, "(&s&sx)", &account, &key, &index))
    {
        gchar *joined;
        gint64 *copy;

        if (!name_ok (account) || !name_ok (key))
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Lockout state has an invalid name");
        joined = make_key (account, key);
        if (g_hash_table_contains (staged, joined))
        {
            g_free (joined);
            return _lrg_mmo_fail (error, G_IO_ERROR_INVALID_DATA, "Lockout state repeats an entry");
        }
        copy = g_new (gint64, 1);
        *copy = index;
        g_hash_table_replace (staged, joined, copy);
    }
    g_hash_table_remove_all (self->locks);
    {
        GHashTableIter it;
        gpointer k;
        gpointer v;

        g_hash_table_iter_init (&it, staged);
        while (g_hash_table_iter_next (&it, &k, &v))
        {
            g_hash_table_iter_steal (&it);
            g_hash_table_replace (self->locks, k, v);
        }
    }
    return TRUE;
}
