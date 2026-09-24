/* lrg-collection.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Owned collectibles keyed by id (unique across kinds). Each entry stores
 * its kind and a favourite flag; one active id is kept per kind. Every
 * mutating call validates first and commits last so failures never leave
 * partial state behind.
 */

#include "config.h"

#include <string.h>

#ifndef LIBREGNUM_COMPILATION
#define LIBREGNUM_COMPILATION
#endif
#include "collection/lrg-collection.h"

/* Number of LrgCollectibleKind values (MOUNT, PET, TITLE, TOY). */
#define COLLECTION_N_KINDS ((guint)LRG_COLLECTIBLE_KIND_TOY + 1)

typedef struct
{
    LrgCollectibleKind kind;
    gboolean           favorite;
} CollectionEntry;

struct _LrgCollection
{
    GObject     parent_instance;

    GHashTable *entries;                    /* gchar* id -> CollectionEntry* */
    gchar      *active[COLLECTION_N_KINDS]; /* owned copies, nullable */
};

G_DEFINE_TYPE (LrgCollection, lrg_collection, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------ */
/* Helpers                                                                  */
/* ------------------------------------------------------------------------ */

static gboolean
kind_is_valid (guint kind)
{
    return kind < COLLECTION_N_KINDS;
}

/* id_is_valid:
 * Identifiers must be non-empty, at most LRG_COLLECTION_MAX_ID_LENGTH
 * bytes and valid UTF-8. */
static gboolean
id_is_valid (const gchar *id)
{
    gsize len;

    if (id == NULL)
        return FALSE;

    len = strnlen (id, LRG_COLLECTION_MAX_ID_LENGTH + 1);
    if (len == 0 || len > LRG_COLLECTION_MAX_ID_LENGTH)
        return FALSE;

    return g_utf8_validate (id, (gssize)len, NULL);
}

static gint
compare_id_ptrs (gconstpointer a,
                 gconstpointer b)
{
    return g_strcmp0 (*(const gchar * const *)a, *(const gchar * const *)b);
}

/* collect_ids:
 * Returns the ids of @kind (optionally only favourites) sorted with
 * g_strcmp0. The strings are borrowed from the hash table keys. */
static GPtrArray *
collect_ids (LrgCollection      *self,
             LrgCollectibleKind  kind,
             gboolean            favorites_only)
{
    GPtrArray      *ids;
    GHashTableIter  iter;
    gpointer        key;
    gpointer        value;

    ids = g_ptr_array_new ();
    if (!kind_is_valid ((guint)kind))
        return ids;

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        CollectionEntry *entry = value;

        if (entry->kind != kind)
            continue;
        if (favorites_only && !entry->favorite)
            continue;
        g_ptr_array_add (ids, key);
    }

    g_ptr_array_sort (ids, compare_id_ptrs);
    return ids;
}

/* ------------------------------------------------------------------------ */
/* GObject                                                                  */
/* ------------------------------------------------------------------------ */

static void
lrg_collection_finalize (GObject *object)
{
    LrgCollection *self = LRG_COLLECTION (object);
    guint          i;

    g_clear_pointer (&self->entries, g_hash_table_unref);
    for (i = 0; i < COLLECTION_N_KINDS; i++)
        g_clear_pointer (&self->active[i], g_free);

    G_OBJECT_CLASS (lrg_collection_parent_class)->finalize (object);
}

static void
lrg_collection_class_init (LrgCollectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_collection_finalize;
}

static void
lrg_collection_init (LrgCollection *self)
{
    self->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, g_free);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

LrgCollection *
lrg_collection_new (void)
{
    return g_object_new (LRG_TYPE_COLLECTION, NULL);
}

gboolean
lrg_collection_add (LrgCollection       *self,
                    LrgCollectibleKind   kind,
                    const gchar         *id,
                    GError             **error)
{
    CollectionEntry *entry;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Validate everything before touching state. */
    if (!kind_is_valid ((guint)kind))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Unknown collectible kind %u", (guint)kind);
        return FALSE;
    }
    if (!id_is_valid (id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Collectible id must be 1..%d bytes of valid UTF-8",
                     LRG_COLLECTION_MAX_ID_LENGTH);
        return FALSE;
    }
    if (g_hash_table_contains (self->entries, id))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_DUPLICATE,
                     "Collectible '%s' is already owned", id);
        return FALSE;
    }
    if (g_hash_table_size (self->entries) >= LRG_COLLECTION_MAX_ENTRIES)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_LIMIT,
                     "Collection is full (%d entries)", LRG_COLLECTION_MAX_ENTRIES);
        return FALSE;
    }

    entry = g_new0 (CollectionEntry, 1);
    entry->kind = kind;
    entry->favorite = FALSE;
    g_hash_table_insert (self->entries, g_strdup (id), entry);

    return TRUE;
}

gboolean
lrg_collection_remove (LrgCollection *self,
                       const gchar   *id)
{
    CollectionEntry *entry;
    guint            kind;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);

    if (id == NULL)
        return FALSE;

    entry = g_hash_table_lookup (self->entries, id);
    if (entry == NULL)
        return FALSE;

    /* Clear the active slot before the key string is freed. */
    kind = (guint)entry->kind;
    if (g_strcmp0 (self->active[kind], id) == 0)
        g_clear_pointer (&self->active[kind], g_free);

    g_hash_table_remove (self->entries, id);
    return TRUE;
}

gboolean
lrg_collection_has (LrgCollection *self,
                    const gchar   *id)
{
    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);

    if (id == NULL)
        return FALSE;

    return g_hash_table_contains (self->entries, id);
}

gboolean
lrg_collection_get_kind (LrgCollection      *self,
                         const gchar        *id,
                         LrgCollectibleKind *out_kind)
{
    CollectionEntry *entry;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);

    if (id == NULL)
        return FALSE;

    entry = g_hash_table_lookup (self->entries, id);
    if (entry == NULL)
        return FALSE;

    if (out_kind != NULL)
        *out_kind = entry->kind;
    return TRUE;
}

guint
lrg_collection_get_count (LrgCollection      *self,
                          LrgCollectibleKind  kind)
{
    GHashTableIter  iter;
    gpointer        value;
    guint           count;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), 0);

    count = 0;
    if (!kind_is_valid ((guint)kind))
        return 0;

    g_hash_table_iter_init (&iter, self->entries);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        if (((CollectionEntry *)value)->kind == kind)
            count++;
    }

    return count;
}

guint
lrg_collection_get_total_count (LrgCollection *self)
{
    g_return_val_if_fail (LRG_IS_COLLECTION (self), 0);

    return g_hash_table_size (self->entries);
}

GPtrArray *
lrg_collection_get_ids (LrgCollection      *self,
                        LrgCollectibleKind  kind)
{
    g_return_val_if_fail (LRG_IS_COLLECTION (self), NULL);

    return collect_ids (self, kind, FALSE);
}

GPtrArray *
lrg_collection_get_favorites (LrgCollection      *self,
                              LrgCollectibleKind  kind)
{
    g_return_val_if_fail (LRG_IS_COLLECTION (self), NULL);

    return collect_ids (self, kind, TRUE);
}

gboolean
lrg_collection_set_favorite (LrgCollection  *self,
                             const gchar    *id,
                             gboolean        favorite,
                             GError        **error)
{
    CollectionEntry *entry;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    entry = (id != NULL) ? g_hash_table_lookup (self->entries, id) : NULL;
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "Collectible '%s' is not owned", id != NULL ? id : "(null)");
        return FALSE;
    }

    entry->favorite = !!favorite;
    return TRUE;
}

gboolean
lrg_collection_is_favorite (LrgCollection *self,
                            const gchar   *id)
{
    CollectionEntry *entry;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);

    if (id == NULL)
        return FALSE;

    entry = g_hash_table_lookup (self->entries, id);
    return entry != NULL && entry->favorite;
}

gboolean
lrg_collection_set_active (LrgCollection       *self,
                           LrgCollectibleKind   kind,
                           const gchar         *id,
                           GError             **error)
{
    CollectionEntry *entry;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!kind_is_valid ((guint)kind))
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Unknown collectible kind %u", (guint)kind);
        return FALSE;
    }

    /* NULL clears the selection for this kind. */
    if (id == NULL)
    {
        g_clear_pointer (&self->active[kind], g_free);
        return TRUE;
    }

    entry = g_hash_table_lookup (self->entries, id);
    if (entry == NULL)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_NOT_FOUND,
                     "Collectible '%s' is not owned", id);
        return FALSE;
    }
    if (entry->kind != kind)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Collectible '%s' is kind %u, not kind %u",
                     id, (guint)entry->kind, (guint)kind);
        return FALSE;
    }

    if (g_strcmp0 (self->active[kind], id) != 0)
    {
        g_free (self->active[kind]);
        self->active[kind] = g_strdup (id);
    }

    return TRUE;
}

const gchar *
lrg_collection_get_active (LrgCollection      *self,
                           LrgCollectibleKind  kind)
{
    g_return_val_if_fail (LRG_IS_COLLECTION (self), NULL);

    if (!kind_is_valid ((guint)kind))
        return NULL;

    return self->active[kind];
}

/* ------------------------------------------------------------------------ */
/* Persistence                                                              */
/* ------------------------------------------------------------------------ */

GVariant *
lrg_collection_to_variant (LrgCollection *self)
{
    GVariantBuilder entries;
    GVariantBuilder actives;
    guint           kind;
    guint           i;

    g_return_val_if_fail (LRG_IS_COLLECTION (self), NULL);

    g_variant_builder_init (&entries, G_VARIANT_TYPE ("a(usb)"));
    g_variant_builder_init (&actives, G_VARIANT_TYPE ("a(us)"));

    /* Iterating kinds in order and ids sorted gives (kind, id) order. */
    for (kind = 0; kind < COLLECTION_N_KINDS; kind++)
    {
        g_autoptr(GPtrArray) ids = NULL;

        ids = collect_ids (self, (LrgCollectibleKind)kind, FALSE);
        for (i = 0; i < ids->len; i++)
        {
            const gchar     *id = g_ptr_array_index (ids, i);
            CollectionEntry *entry = g_hash_table_lookup (self->entries, id);

            g_variant_builder_add (&entries, "(usb)", kind, id, entry->favorite);
        }

        if (self->active[kind] != NULL)
            g_variant_builder_add (&actives, "(us)", kind, self->active[kind]);
    }

    return g_variant_ref_sink (g_variant_new ("(a(usb)a(us))", &entries, &actives));
}

/* restore_into:
 * Validates @variant and fills @self. Returns FALSE with @error set on the
 * first violation; the caller discards @self in that case. */
static gboolean
restore_into (LrgCollection  *self,
              GVariant       *variant,
              GError        **error)
{
    g_autoptr(GVariant) entries = NULL;
    g_autoptr(GVariant) actives = NULL;
    gsize               n;
    gsize               i;

    /* Exact type and canonical encoding first: everything after this can
     * trust the structure. Normal form also guarantees valid UTF-8. */
    if (g_strcmp0 (g_variant_get_type_string (variant), LRG_COLLECTION_VARIANT_TYPE) != 0)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Collection variant must have type %s, got %s",
                     LRG_COLLECTION_VARIANT_TYPE, g_variant_get_type_string (variant));
        return FALSE;
    }
    if (!g_variant_is_normal_form (variant))
    {
        g_set_error_literal (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                             "Collection variant is not in normal form");
        return FALSE;
    }

    entries = g_variant_get_child_value (variant, 0);
    actives = g_variant_get_child_value (variant, 1);

    /* Owned entries. */
    n = g_variant_n_children (entries);
    if (n > LRG_COLLECTION_MAX_ENTRIES)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Collection has %" G_GSIZE_FORMAT " entries (max %d)",
                     n, LRG_COLLECTION_MAX_ENTRIES);
        return FALSE;
    }
    for (i = 0; i < n; i++)
    {
        guint32          kind;
        const gchar     *id;
        gboolean         favorite;
        CollectionEntry *entry;

        g_variant_get_child (entries, i, "(u&sb)", &kind, &id, &favorite);
        if (!kind_is_valid (kind))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Entry %" G_GSIZE_FORMAT " has unknown kind %u", i, kind);
            return FALSE;
        }
        if (!id_is_valid (id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Entry %" G_GSIZE_FORMAT " has an invalid id", i);
            return FALSE;
        }
        if (g_hash_table_contains (self->entries, id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Duplicate collectible id '%s'", id);
            return FALSE;
        }

        entry = g_new0 (CollectionEntry, 1);
        entry->kind = (LrgCollectibleKind)kind;
        entry->favorite = !!favorite;
        g_hash_table_insert (self->entries, g_strdup (id), entry);
    }

    /* Active selections: at most one per kind, each owned with that kind. */
    n = g_variant_n_children (actives);
    if (n > COLLECTION_N_KINDS)
    {
        g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                     "Collection has %" G_GSIZE_FORMAT " active rows (max %u)",
                     n, COLLECTION_N_KINDS);
        return FALSE;
    }
    for (i = 0; i < n; i++)
    {
        guint32          kind;
        const gchar     *id;
        CollectionEntry *entry;

        g_variant_get_child (actives, i, "(u&s)", &kind, &id);
        if (!kind_is_valid (kind))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Active row %" G_GSIZE_FORMAT " has unknown kind %u", i, kind);
            return FALSE;
        }
        if (self->active[kind] != NULL)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Duplicate active row for kind %u", kind);
            return FALSE;
        }
        if (!id_is_valid (id))
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Active row %" G_GSIZE_FORMAT " has an invalid id", i);
            return FALSE;
        }
        entry = g_hash_table_lookup (self->entries, id);
        if (entry == NULL)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Active collectible '%s' is not owned", id);
            return FALSE;
        }
        if ((guint)entry->kind != kind)
        {
            g_set_error (error, LRG_PROGRESSION_ERROR, LRG_PROGRESSION_ERROR_INVALID,
                         "Active collectible '%s' is not of kind %u", id, kind);
            return FALSE;
        }

        self->active[kind] = g_strdup (id);
    }

    return TRUE;
}

LrgCollection *
lrg_collection_new_from_variant (GVariant  *variant,
                                 GError   **error)
{
    g_autoptr(GVariant)      owned = NULL;
    g_autoptr(LrgCollection) self = NULL;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    /* Sink a floating reference (consumes it) or add one we drop below. */
    owned = g_variant_ref_sink (variant);
    self = lrg_collection_new ();

    if (!restore_into (self, owned, error))
        return NULL;

    return (LrgCollection *)g_steal_pointer (&self);
}
