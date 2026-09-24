/* lrg-spellbook.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Known ability ids plus an action bar, with validated persistence.
 */

#include "config.h"

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "progression/lrg-spellbook.h"

#include <string.h>

#define LRG_SPELLBOOK_MAX_ID_LENGTH (128)

struct _LrgSpellbook
{
    GObject     parent_instance;

    GHashTable *known;                             /* gchar* set */
    guint       bar_size;
    gchar      *bar[LRG_SPELLBOOK_MAX_BAR_SIZE];   /* owned, NULL = empty */
};

G_DEFINE_TYPE (LrgSpellbook, lrg_spellbook, G_TYPE_OBJECT)

/*
 * id_is_valid:
 *
 * Ids must be non-empty, at most 128 bytes and valid UTF-8.
 */
static gboolean
id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;
    len = strlen (id);
    if (len == 0 || len > LRG_SPELLBOOK_MAX_ID_LENGTH)
        return FALSE;
    return g_utf8_validate (id, (gssize) len, NULL);
}

static void
lrg_spellbook_finalize (GObject *object)
{
    LrgSpellbook *self = LRG_SPELLBOOK (object);
    guint i;

    g_clear_pointer (&self->known, g_hash_table_unref);
    for (i = 0; i < LRG_SPELLBOOK_MAX_BAR_SIZE; i++)
        g_clear_pointer (&self->bar[i], g_free);

    G_OBJECT_CLASS (lrg_spellbook_parent_class)->finalize (object);
}

static void
lrg_spellbook_class_init (LrgSpellbookClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_spellbook_finalize;
}

static void
lrg_spellbook_init (LrgSpellbook *self)
{
    self->known = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->bar_size = LRG_SPELLBOOK_DEFAULT_BAR_SIZE;
    memset (self->bar, 0, sizeof (self->bar));
}

LrgSpellbook *
lrg_spellbook_new (void)
{
    return g_object_new (LRG_TYPE_SPELLBOOK, NULL);
}

gboolean
lrg_spellbook_learn (LrgSpellbook  *self,
                     const gchar   *ability_id,
                     GError       **error)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!id_is_valid (ability_id))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "ability id must be 1-128 bytes of valid UTF-8");
        return FALSE;
    }
    if (g_hash_table_contains (self->known, ability_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "ability '%s' is already known", ability_id);
        return FALSE;
    }
    if (g_hash_table_size (self->known) >= LRG_SPELLBOOK_MAX_KNOWN)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "spellbook already knows %u abilities", LRG_SPELLBOOK_MAX_KNOWN);
        return FALSE;
    }

    g_hash_table_add (self->known, g_strdup (ability_id));
    return TRUE;
}

gboolean
lrg_spellbook_forget (LrgSpellbook *self,
                      const gchar  *ability_id)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), FALSE);

    if (ability_id == NULL || !g_hash_table_contains (self->known, ability_id))
        return FALSE;

    /* clear bar slots first while ability_id may still be a hash key */
    for (i = 0; i < self->bar_size; i++)
    {
        if (g_strcmp0 (self->bar[i], ability_id) == 0)
            g_clear_pointer (&self->bar[i], g_free);
    }
    g_hash_table_remove (self->known, ability_id);
    return TRUE;
}

gboolean
lrg_spellbook_knows (LrgSpellbook *self,
                     const gchar  *ability_id)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), FALSE);

    if (ability_id == NULL)
        return FALSE;
    return g_hash_table_contains (self->known, ability_id);
}

static gint
compare_strings (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

GPtrArray *
lrg_spellbook_get_known (LrgSpellbook *self)
{
    GPtrArray *ids;
    GHashTableIter iter;
    gpointer key;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), NULL);

    ids = g_ptr_array_sized_new (g_hash_table_size (self->known));
    g_hash_table_iter_init (&iter, self->known);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (ids, key);
    g_ptr_array_sort (ids, compare_strings);
    return ids;
}

guint
lrg_spellbook_get_known_count (LrgSpellbook *self)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), 0);

    return g_hash_table_size (self->known);
}

/* Orders definitions by (min-level, id). */
static gint
compare_defs (gconstpointer a,
              gconstpointer b)
{
    LrgAbilityDef *da = *(LrgAbilityDef * const *) a;
    LrgAbilityDef *db = *(LrgAbilityDef * const *) b;
    guint la;
    guint lb;

    la = lrg_ability_def_get_min_level (da);
    lb = lrg_ability_def_get_min_level (db);
    if (la != lb)
        return (la < lb) ? -1 : 1;
    return g_strcmp0 (lrg_ability_def_get_id (da), lrg_ability_def_get_id (db));
}

GPtrArray *
lrg_spellbook_get_learnable (LrgSpellbook          *self,
                             GPtrArray             *defs,
                             const gchar           *class_id,
                             guint                  level,
                             const gchar           *spec_id,
                             LrgAbilityLearnSource  source)
{
    g_autoptr(GHashTable) seen = NULL;
    GPtrArray *result;
    guint i;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), NULL);
    g_return_val_if_fail (defs != NULL, NULL);

    seen = g_hash_table_new (g_str_hash, g_str_equal);
    result = g_ptr_array_new ();
    for (i = 0; i < defs->len; i++)
    {
        LrgAbilityDef *def = g_ptr_array_index (defs, i);
        const gchar *id;

        if (!LRG_IS_ABILITY_DEF (def))
            continue;
        id = lrg_ability_def_get_id (def);
        if (id == NULL)
            continue;
        if (lrg_ability_def_get_learn_source (def) != source)
            continue;
        if (lrg_spellbook_knows (self, id))
            continue;
        if (!lrg_ability_def_is_eligible (def, class_id, level, spec_id))
            continue;
        /* two definitions sharing an id are offered once (first wins) */
        if (!g_hash_table_add (seen, (gpointer) id))
            continue;
        g_ptr_array_add (result, def);
    }
    g_ptr_array_sort (result, compare_defs);
    return result;
}

guint
lrg_spellbook_learn_automatic (LrgSpellbook *self,
                               GPtrArray    *defs,
                               const gchar  *class_id,
                               guint         level,
                               const gchar  *spec_id)
{
    g_autoptr(GPtrArray) learnable = NULL;
    guint learned;
    guint i;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), 0);
    g_return_val_if_fail (defs != NULL, 0);

    learnable = lrg_spellbook_get_learnable (self, defs, class_id, level, spec_id,
                                             LRG_ABILITY_LEARN_SOURCE_AUTO);
    learned = 0;
    for (i = 0; i < learnable->len; i++)
    {
        LrgAbilityDef *def = g_ptr_array_index (learnable, i);

        /* duplicate ids, invalid ids and a full book are skipped silently */
        if (lrg_spellbook_learn (self, lrg_ability_def_get_id (def), NULL))
            learned++;
    }
    return learned;
}

guint
lrg_spellbook_get_bar_size (LrgSpellbook *self)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), 0);

    return self->bar_size;
}

gboolean
lrg_spellbook_set_bar_size (LrgSpellbook  *self,
                            guint          size,
                            GError       **error)
{
    guint i;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (size < 1 || size > LRG_SPELLBOOK_MAX_BAR_SIZE)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "bar size %u is outside 1..%u", size, LRG_SPELLBOOK_MAX_BAR_SIZE);
        return FALSE;
    }

    /* drop bindings in slots that no longer exist */
    for (i = size; i < LRG_SPELLBOOK_MAX_BAR_SIZE; i++)
        g_clear_pointer (&self->bar[i], g_free);
    self->bar_size = size;
    return TRUE;
}

gboolean
lrg_spellbook_bind (LrgSpellbook  *self,
                    guint          slot,
                    const gchar   *ability_id,
                    GError       **error)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (slot >= self->bar_size)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "slot %u is outside the %u-slot bar", slot, self->bar_size);
        return FALSE;
    }
    if (ability_id != NULL && !g_hash_table_contains (self->known, ability_id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "ability '%s' is not known", ability_id);
        return FALSE;
    }

    g_free (self->bar[slot]);
    self->bar[slot] = g_strdup (ability_id);
    return TRUE;
}

const gchar *
lrg_spellbook_get_binding (LrgSpellbook *self,
                           guint         slot)
{
    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), NULL);

    if (slot >= self->bar_size)
        return NULL;
    return self->bar[slot];
}

GVariant *
lrg_spellbook_to_variant (LrgSpellbook *self)
{
    g_autoptr(GPtrArray) known = NULL;
    GVariantBuilder known_builder;
    GVariantBuilder bar_builder;
    GVariant *result;
    guint i;

    g_return_val_if_fail (LRG_IS_SPELLBOOK (self), NULL);

    known = lrg_spellbook_get_known (self);
    g_variant_builder_init (&known_builder, G_VARIANT_TYPE_STRING_ARRAY);
    for (i = 0; i < known->len; i++)
        g_variant_builder_add (&known_builder, "s", (const gchar *) g_ptr_array_index (known, i));

    g_variant_builder_init (&bar_builder, G_VARIANT_TYPE_STRING_ARRAY);
    for (i = 0; i < self->bar_size; i++)
        g_variant_builder_add (&bar_builder, "s", self->bar[i] != NULL ? self->bar[i] : "");

    result = g_variant_new ("(asuas)", &known_builder, self->bar_size, &bar_builder);
    return g_variant_ref_sink (result);
}

LrgSpellbook *
lrg_spellbook_new_from_variant (GVariant  *variant,
                                GError   **error)
{
    g_autoptr(LrgSpellbook) book = NULL;
    g_autoptr(GVariant) known = NULL;
    g_autoptr(GVariant) bar = NULL;
    guint32 bar_size;
    gsize n;
    gsize i;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_SPELLBOOK_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "spellbook variant has type '%s', expected '%s'",
                     g_variant_get_type_string (variant), LRG_SPELLBOOK_VARIANT_TYPE);
        return NULL;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "spellbook variant is not in normal form");
        return NULL;
    }

    known = g_variant_get_child_value (variant, 0);
    g_variant_get_child (variant, 1, "u", &bar_size);
    bar = g_variant_get_child_value (variant, 2);

    n = g_variant_n_children (known);
    if (n > LRG_SPELLBOOK_MAX_KNOWN)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "spellbook variant knows %" G_GSIZE_FORMAT " abilities, max %u",
                     n, LRG_SPELLBOOK_MAX_KNOWN);
        return NULL;
    }
    if (bar_size < 1 || bar_size > LRG_SPELLBOOK_MAX_BAR_SIZE)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "spellbook variant bar size %u is outside 1..%u",
                     bar_size, LRG_SPELLBOOK_MAX_BAR_SIZE);
        return NULL;
    }
    if (g_variant_n_children (bar) != bar_size)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "spellbook variant has %" G_GSIZE_FORMAT " slots for bar size %u",
                     g_variant_n_children (bar), bar_size);
        return NULL;
    }

    book = lrg_spellbook_new ();

    /* known ids: valid and unique */
    for (i = 0; i < n; i++)
    {
        const gchar *id;

        g_variant_get_child (known, i, "&s", &id);
        if (!id_is_valid (id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "spellbook variant known id %" G_GSIZE_FORMAT " is invalid", i);
            return NULL;
        }
        if (g_hash_table_contains (book->known, id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "spellbook variant repeats ability '%s'", id);
            return NULL;
        }
        g_hash_table_add (book->known, g_strdup (id));
    }

    /* bar slots: "" or a known id */
    book->bar_size = bar_size;
    for (i = 0; i < bar_size; i++)
    {
        const gchar *id;

        g_variant_get_child (bar, i, "&s", &id);
        if (*id == '\0')
            continue;
        if (!g_hash_table_contains (book->known, id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "spellbook variant slot %" G_GSIZE_FORMAT " binds unknown ability", i);
            return NULL;
        }
        book->bar[i] = g_strdup (id);
    }

    return g_steal_pointer (&book);
}
