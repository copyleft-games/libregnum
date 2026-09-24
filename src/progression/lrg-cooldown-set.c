/* lrg-cooldown-set.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Remaining-time tables for ability keys, shared categories and the global
 * cooldown. Time only advances through lrg_cooldown_set_tick().
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-cooldown-set.h"

#include <math.h>
#include <string.h>

#define LRG_COOLDOWN_MAX_ID_LENGTH (128)

struct _LrgCooldownSet
{
    GObject     parent_instance;

    gdouble     global_duration;
    gdouble     global;
    GHashTable *keys;         /* gchar* -> gdouble* */
    GHashTable *categories;   /* gchar* -> gdouble* */
};

G_DEFINE_TYPE (LrgCooldownSet, lrg_cooldown_set, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_GLOBAL_DURATION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static gboolean
id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;
    len = strlen (id);
    if (len == 0 || len > LRG_COOLDOWN_MAX_ID_LENGTH)
        return FALSE;
    return g_utf8_validate (id, (gssize) len, NULL);
}

static GHashTable *
new_table (void)
{
    return g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static gdouble
table_get (GHashTable  *table,
           const gchar *name)
{
    gdouble *value;

    if (name == NULL)
        return 0.0;
    value = g_hash_table_lookup (table, name);
    return value != NULL ? *value : 0.0;
}

/*
 * evict_smallest:
 *
 * Drops the entry with the least remaining time (ties: smallest name) so a
 * full table can accept a new cooldown without exceeding the persisted bound.
 */
static void
evict_smallest (GHashTable *table)
{
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    const gchar *victim;
    gdouble victim_left;

    victim = NULL;
    victim_left = 0.0;
    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        gdouble left = *(gdouble *) value;

        if (victim == NULL || left < victim_left ||
            (left == victim_left && g_strcmp0 (key, victim) < 0))
        {
            victim = key;
            victim_left = left;
        }
    }
    if (victim != NULL)
        g_hash_table_remove (table, victim);
}

/*
 * table_start:
 *
 * Shared implementation for key and category cooldowns.
 */
static void
table_start (GHashTable  *table,
             const gchar *name,
             gdouble      seconds)
{
    gdouble *slot;

    if (!(seconds > 0.0))
    {
        g_hash_table_remove (table, name);
        return;
    }
    if (seconds > LRG_COOLDOWN_SET_MAX_SECONDS)
        seconds = LRG_COOLDOWN_SET_MAX_SECONDS;

    slot = g_hash_table_lookup (table, name);
    if (slot != NULL)
    {
        *slot = seconds;
        return;
    }
    if (g_hash_table_size (table) >= LRG_COOLDOWN_SET_MAX_ENTRIES)
        evict_smallest (table);
    slot = g_new (gdouble, 1);
    *slot = seconds;
    g_hash_table_insert (table, g_strdup (name), slot);
}

/* Advances a table by delta, removing finished entries. */
static void
table_tick (GHashTable *table,
            gdouble     delta)
{
    GHashTableIter iter;
    gpointer value;

    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        gdouble *left = value;

        *left -= delta;
        if (!(*left > 0.0))
            g_hash_table_iter_remove (&iter);
    }
}

static void
lrg_cooldown_set_finalize (GObject *object)
{
    LrgCooldownSet *self = LRG_COOLDOWN_SET (object);

    g_clear_pointer (&self->keys, g_hash_table_unref);
    g_clear_pointer (&self->categories, g_hash_table_unref);

    G_OBJECT_CLASS (lrg_cooldown_set_parent_class)->finalize (object);
}

static void
lrg_cooldown_set_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgCooldownSet *self = LRG_COOLDOWN_SET (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_DURATION:
        g_value_set_double (value, self->global_duration);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_cooldown_set_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgCooldownSet *self = LRG_COOLDOWN_SET (object);

    switch (prop_id)
    {
    case PROP_GLOBAL_DURATION:
        lrg_cooldown_set_set_global_duration (self, g_value_get_double (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_cooldown_set_class_init (LrgCooldownSetClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_cooldown_set_finalize;
    object_class->get_property = lrg_cooldown_set_get_property;
    object_class->set_property = lrg_cooldown_set_set_property;

    /**
     * LrgCooldownSet:global-duration:
     *
     * Seconds lrg_cooldown_set_start_ability() puts on the global cooldown.
     */
    properties[PROP_GLOBAL_DURATION] =
        g_param_spec_double ("global-duration", NULL, "Global cooldown length",
                             0.0, LRG_COOLDOWN_SET_MAX_SECONDS, 1.0,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |
                             G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
lrg_cooldown_set_init (LrgCooldownSet *self)
{
    self->global_duration = 1.0;
    self->global = 0.0;
    self->keys = new_table ();
    self->categories = new_table ();
}

LrgCooldownSet *
lrg_cooldown_set_new (void)
{
    return g_object_new (LRG_TYPE_COOLDOWN_SET, NULL);
}

gdouble
lrg_cooldown_set_get_global_duration (LrgCooldownSet *self)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), 0.0);

    return self->global_duration;
}

void
lrg_cooldown_set_set_global_duration (LrgCooldownSet *self,
                                      gdouble         seconds)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));
    g_return_if_fail (isfinite (seconds) && seconds >= 0.0 &&
                      seconds <= LRG_COOLDOWN_SET_MAX_SECONDS);

    if (self->global_duration == seconds)
        return;
    self->global_duration = seconds;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GLOBAL_DURATION]);
}

void
lrg_cooldown_set_start (LrgCooldownSet *self,
                        const gchar    *key,
                        gdouble         seconds)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));
    g_return_if_fail (id_is_valid (key));
    g_return_if_fail (isfinite (seconds));

    table_start (self->keys, key, seconds);
}

void
lrg_cooldown_set_start_category (LrgCooldownSet *self,
                                 const gchar    *category,
                                 gdouble         seconds)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));
    g_return_if_fail (id_is_valid (category));
    g_return_if_fail (isfinite (seconds));

    table_start (self->categories, category, seconds);
}

void
lrg_cooldown_set_start_global (LrgCooldownSet *self,
                               gdouble         seconds)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));
    g_return_if_fail (isfinite (seconds));

    self->global = CLAMP (seconds, 0.0, LRG_COOLDOWN_SET_MAX_SECONDS);
}

gdouble
lrg_cooldown_set_get_remaining (LrgCooldownSet *self,
                                const gchar    *key)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), 0.0);

    return table_get (self->keys, key);
}

gdouble
lrg_cooldown_set_get_category_remaining (LrgCooldownSet *self,
                                         const gchar    *category)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), 0.0);

    return table_get (self->categories, category);
}

gdouble
lrg_cooldown_set_get_global_remaining (LrgCooldownSet *self)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), 0.0);

    return self->global;
}

gboolean
lrg_cooldown_set_is_ready (LrgCooldownSet *self,
                           const gchar    *key,
                           const gchar    *category,
                           gboolean        uses_global)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), FALSE);

    if (table_get (self->keys, key) > 0.0)
        return FALSE;
    if (table_get (self->categories, category) > 0.0)
        return FALSE;
    if (uses_global && self->global > 0.0)
        return FALSE;
    return TRUE;
}

void
lrg_cooldown_set_reset (LrgCooldownSet *self,
                        const gchar    *key)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));

    if (key != NULL)
        g_hash_table_remove (self->keys, key);
}

void
lrg_cooldown_set_clear (LrgCooldownSet *self)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));

    g_hash_table_remove_all (self->keys);
    g_hash_table_remove_all (self->categories);
    self->global = 0.0;
}

void
lrg_cooldown_set_tick (LrgCooldownSet *self,
                       gdouble         delta)
{
    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));

    /* untrusted frame deltas: ignore anything that is not a positive number */
    if (!isfinite (delta) || !(delta > 0.0))
        return;

    table_tick (self->keys, delta);
    table_tick (self->categories, delta);
    self->global -= delta;
    if (!(self->global > 0.0))
        self->global = 0.0;
}

guint
lrg_cooldown_set_get_count (LrgCooldownSet *self)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), 0);

    return g_hash_table_size (self->keys);
}

gboolean
lrg_cooldown_set_ability_ready (LrgCooldownSet *self,
                                LrgAbilityDef  *def)
{
    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), FALSE);
    g_return_val_if_fail (LRG_IS_ABILITY_DEF (def), FALSE);

    return lrg_cooldown_set_is_ready (self,
                                      lrg_ability_def_get_id (def),
                                      lrg_ability_def_get_category (def),
                                      lrg_ability_def_get_triggers_gcd (def));
}

void
lrg_cooldown_set_start_ability (LrgCooldownSet *self,
                                LrgAbilityDef  *def)
{
    const gchar *id;
    const gchar *category;

    g_return_if_fail (LRG_IS_COOLDOWN_SET (self));
    g_return_if_fail (LRG_IS_ABILITY_DEF (def));

    id = lrg_ability_def_get_id (def);
    category = lrg_ability_def_get_category (def);

    if (lrg_ability_def_get_cooldown (def) > 0.0 && id_is_valid (id))
        table_start (self->keys, id, lrg_ability_def_get_cooldown (def));
    if (category != NULL && lrg_ability_def_get_category_cooldown (def) > 0.0 &&
        id_is_valid (category))
        table_start (self->categories, category, lrg_ability_def_get_category_cooldown (def));
    if (lrg_ability_def_get_triggers_gcd (def))
        self->global = self->global_duration;
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* Builds a sorted a(sd) array from a table. */
static GVariant *
table_to_variant (GHashTable *table)
{
    g_autoptr(GPtrArray) names = NULL;
    GVariantBuilder builder;
    GHashTableIter iter;
    gpointer key;
    guint i;

    names = g_ptr_array_sized_new (g_hash_table_size (table));
    g_hash_table_iter_init (&iter, table);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (names, key);
    g_ptr_array_sort (names, compare_strings);

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sd)"));
    for (i = 0; i < names->len; i++)
    {
        const gchar *name = g_ptr_array_index (names, i);

        g_variant_builder_add (&builder, "(sd)", name, table_get (table, name));
    }
    return g_variant_builder_end (&builder);
}

GVariant *
lrg_cooldown_set_to_variant (LrgCooldownSet *self)
{
    GVariant *result;

    g_return_val_if_fail (LRG_IS_COOLDOWN_SET (self), NULL);

    result = g_variant_new ("(d@a(sd)@a(sd))", self->global,
                            table_to_variant (self->keys),
                            table_to_variant (self->categories));
    return g_variant_ref_sink (result);
}

static gboolean
seconds_are_valid (gdouble seconds)
{
    return isfinite (seconds) && seconds >= 0.0 && seconds <= LRG_COOLDOWN_SET_MAX_SECONDS;
}

/*
 * table_from_variant:
 *
 * Validates and loads one a(sd) array into @table.
 */
static gboolean
table_from_variant (GHashTable   *table,
                    GVariant     *array,
                    const gchar  *what,
                    GError      **error)
{
    gsize n;
    gsize i;

    n = g_variant_n_children (array);
    if (n > LRG_COOLDOWN_SET_MAX_ENTRIES)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "cooldown variant has %" G_GSIZE_FORMAT " %s entries, max %u",
                     n, what, LRG_COOLDOWN_SET_MAX_ENTRIES);
        return FALSE;
    }

    for (i = 0; i < n; i++)
    {
        const gchar *name;
        gdouble seconds;
        gdouble *slot;

        g_variant_get_child (array, i, "(&sd)", &name, &seconds);
        if (!id_is_valid (name))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "cooldown variant %s entry %" G_GSIZE_FORMAT " has an invalid name",
                         what, i);
            return FALSE;
        }
        if (!seconds_are_valid (seconds))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "cooldown variant %s '%s' has invalid time", what, name);
            return FALSE;
        }
        if (g_hash_table_contains (table, name))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "cooldown variant repeats %s '%s'", what, name);
            return FALSE;
        }
        /* a zero entry is finished; keep a placeholder only for the
         * duplicate check and drop it after loading */
        slot = g_new (gdouble, 1);
        *slot = seconds;
        g_hash_table_insert (table, g_strdup (name), slot);
    }
    return TRUE;
}

/* Removes finished (zero) entries accepted by table_from_variant(). */
static gboolean
remove_finished (gpointer key,
                 gpointer value,
                 gpointer user_data)
{
    (void) key;
    (void) user_data;
    return !(*(gdouble *) value > 0.0);
}

LrgCooldownSet *
lrg_cooldown_set_new_from_variant (GVariant  *variant,
                                   GError   **error)
{
    g_autoptr(LrgCooldownSet) set = NULL;
    g_autoptr(GVariant) keys = NULL;
    g_autoptr(GVariant) categories = NULL;
    gdouble global;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_COOLDOWN_SET_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "cooldown variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant), LRG_COOLDOWN_SET_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "cooldown variant is not in normal form");
        return NULL;
    }

    g_variant_get_child (variant, 0, "d", &global);
    keys = g_variant_get_child_value (variant, 1);
    categories = g_variant_get_child_value (variant, 2);

    if (!seconds_are_valid (global))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "cooldown variant has an invalid global cooldown");
        return NULL;
    }

    set = lrg_cooldown_set_new ();
    set->global = global;
    if (!table_from_variant (set->keys, keys, "key", error))
        return NULL;
    if (!table_from_variant (set->categories, categories, "category", error))
        return NULL;
    g_hash_table_foreach_remove (set->keys, remove_finished, NULL);
    g_hash_table_foreach_remove (set->categories, remove_finished, NULL);

    return g_steal_pointer (&set);
}
