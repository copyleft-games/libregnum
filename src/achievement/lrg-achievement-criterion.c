/* lrg-achievement-criterion.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgAchievementCriterion - one step of an achievement with its own
 * progress: a count, a value or distinct keys, clamped to the target.
 */

#include "config.h"

#include "lrg-achievement-criterion.h"

struct _LrgAchievementCriterion
{
    gint        ref_count;
    gchar      *id;
    gchar      *description;
    gint64      target;
    gint64      current;
    GPtrArray  *keys;       /* gchar *, first-seen order */
    GHashTable *key_set;    /* gchar * (owned by keys) */
};

G_DEFINE_BOXED_TYPE (LrgAchievementCriterion,
                     lrg_achievement_criterion,
                     lrg_achievement_criterion_ref,
                     lrg_achievement_criterion_unref)

LrgAchievementCriterion *
lrg_achievement_criterion_new (const gchar *id,
                               const gchar *description,
                               gint64       target)
{
    LrgAchievementCriterion *self;

    g_return_val_if_fail (id != NULL && *id != '\0', NULL);

    self = g_new0 (LrgAchievementCriterion, 1);
    self->ref_count = 1;
    self->id = g_strdup (id);
    self->description = g_strdup (description);
    self->target = MAX (target, 1);
    self->keys = g_ptr_array_new_with_free_func (g_free);
    self->key_set = g_hash_table_new (g_str_hash, g_str_equal);

    return self;
}

LrgAchievementCriterion *
lrg_achievement_criterion_ref (LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    g_atomic_int_inc (&self->ref_count);
    return self;
}

void
lrg_achievement_criterion_unref (LrgAchievementCriterion *self)
{
    if (self == NULL)
        return;
    if (!g_atomic_int_dec_and_test (&self->ref_count))
        return;

    g_hash_table_unref (self->key_set);
    g_ptr_array_unref (self->keys);
    g_free (self->id);
    g_free (self->description);
    g_free (self);
}

const gchar *
lrg_achievement_criterion_get_id (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->id;
}

const gchar *
lrg_achievement_criterion_get_description (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, NULL);
    return self->description;
}

gint64
lrg_achievement_criterion_get_target (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, 1);
    return self->target;
}

gint64
lrg_achievement_criterion_get_current (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, 0);
    return self->current;
}

gboolean
lrg_achievement_criterion_set_current (LrgAchievementCriterion *self,
                                       gint64                   value)
{
    gint64 clamped;

    g_return_val_if_fail (self != NULL, FALSE);

    clamped = CLAMP (value, 0, self->target);
    if (clamped == self->current)
        return FALSE;
    self->current = clamped;
    return TRUE;
}

gboolean
lrg_achievement_criterion_increment (LrgAchievementCriterion *self,
                                     gint64                   amount)
{
    g_return_val_if_fail (self != NULL, FALSE);

    /* saturate instead of overflowing on huge amounts */
    if (amount > 0 && self->current > G_MAXINT64 - amount)
        return lrg_achievement_criterion_set_current (self, self->target);
    return lrg_achievement_criterion_set_current (self, self->current + amount);
}

gboolean
lrg_achievement_criterion_add_key (LrgAchievementCriterion *self,
                                   const gchar             *key)
{
    gchar *copy;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    if (g_hash_table_contains (self->key_set, key))
        return FALSE;
    copy = g_strdup (key);
    g_ptr_array_add (self->keys, copy);
    g_hash_table_add (self->key_set, copy);
    lrg_achievement_criterion_increment (self, 1);
    return TRUE;
}

gboolean
lrg_achievement_criterion_has_key (const LrgAchievementCriterion *self,
                                   const gchar                   *key)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->key_set, key);
}

gchar **
lrg_achievement_criterion_get_keys (const LrgAchievementCriterion *self)
{
    gchar **keys;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    keys = g_new0 (gchar *, self->keys->len + 1);
    for (i = 0; i < self->keys->len; i++)
        keys[i] = g_strdup ((const gchar *)g_ptr_array_index (self->keys, i));
    return keys;
}

gboolean
lrg_achievement_criterion_is_complete (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, FALSE);
    return self->current >= self->target;
}

gdouble
lrg_achievement_criterion_get_fraction (const LrgAchievementCriterion *self)
{
    g_return_val_if_fail (self != NULL, 0.0);
    return (gdouble)self->current / (gdouble)self->target;
}

void
lrg_achievement_criterion_reset (LrgAchievementCriterion *self)
{
    g_return_if_fail (self != NULL);

    self->current = 0;
    g_hash_table_remove_all (self->key_set);
    g_ptr_array_set_size (self->keys, 0);
}
