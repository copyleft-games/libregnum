/* lrg-save-document.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * A save document: a format version and a table of sections, each an
 * immutable GVariant state plus a version. Reference counted with
 * g_atomic_rc_box; copies share the (immutable) states.
 */

#include "lrg-save-document.h"

#include <stdlib.h>
#include <string.h>

/* one section: its state (a full reference) and its version */
typedef struct
{
    GVariant *state;
    guint     version;
} Entry;

struct _LrgSaveDocument
{
    guint       format_version;
    GHashTable *sections;    /* name (owned) -> Entry (owned) */
};

static void
entry_free (gpointer data)
{
    Entry *entry = (Entry *) data;

    g_variant_unref (entry->state);
    g_free (entry);
}

static Entry *
entry_new (GVariant *state,
           guint     version)
{
    Entry *entry = g_new0 (Entry, 1);

    entry->state = g_variant_ref_sink (state);
    entry->version = version;
    return entry;
}

/* frees the members when the last reference goes */
static void
document_clear (gpointer data)
{
    LrgSaveDocument *self = (LrgSaveDocument *) data;

    g_hash_table_unref (self->sections);
}

LrgSaveDocument *
lrg_save_document_ref (LrgSaveDocument *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgSaveDocument *) g_atomic_rc_box_acquire (self);
}

void
lrg_save_document_unref (LrgSaveDocument *self)
{
    g_return_if_fail (self != NULL);

    g_atomic_rc_box_release_full (self, document_clear);
}

G_DEFINE_BOXED_TYPE (LrgSaveDocument, lrg_save_document,
                     lrg_save_document_ref, lrg_save_document_unref)

LrgSaveDocument *
lrg_save_document_new (guint format_version)
{
    LrgSaveDocument *self;

    g_return_val_if_fail (format_version > 0, NULL);

    self = g_atomic_rc_box_new0 (LrgSaveDocument);
    self->format_version = format_version;
    self->sections = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, entry_free);
    return self;
}

/*
 * lrg_save_document_new_from_variant:
 * Validates everything before building: the type, the format version,
 * every name, and that every version names a section the document has.
 */
LrgSaveDocument *
lrg_save_document_new_from_variant (GVariant  *variant,
                                    GError   **error)
{
    g_autoptr(LrgSaveDocument) self = NULL;
    g_autoptr(GVariant) sections = NULL;
    g_autoptr(GVariant) versions = NULL;
    GVariantIter iter;
    const gchar *name;
    GVariant *state;
    guint32 format_version;
    guint32 version;

    g_return_val_if_fail (variant != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    if (!g_variant_is_of_type (variant, G_VARIANT_TYPE (LRG_SAVE_DOCUMENT_VARIANT_TYPE)))
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT,
                     "a save document has type %s, not %s",
                     g_variant_get_type_string (variant), LRG_SAVE_DOCUMENT_VARIANT_TYPE);
        return NULL;
    }
    g_variant_get (variant, "(u@a{sv}@a{su})", &format_version, &sections, &versions);
    if (format_version == 0)
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT,
                     "a save document has format version 0");
        return NULL;
    }

    /* --- the sections, unversioned at first --- */
    self = lrg_save_document_new (format_version);
    g_variant_iter_init (&iter, sections);
    while (g_variant_iter_next (&iter, "{&sv}", &name, &state))
    {
        if (*name == '\0' || g_hash_table_contains (self->sections, name))
        {
            g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT,
                         "a save document has an empty or repeated section name \"%s\"", name);
            g_variant_unref (state);
            return NULL;
        }
        g_hash_table_insert (self->sections, g_strdup (name), entry_new (state, 0));
        g_variant_unref (state);
    }

    /* --- their versions --- */
    g_variant_iter_init (&iter, versions);
    while (g_variant_iter_next (&iter, "{&su}", &name, &version))
    {
        Entry *entry = (Entry *) g_hash_table_lookup (self->sections, name);

        if (entry == NULL)
        {
            g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT,
                         "a save document has a version for the missing section \"%s\"", name);
            return NULL;
        }
        entry->version = version;
    }
    return (LrgSaveDocument *) g_steal_pointer (&self);
}

LrgSaveDocument *
lrg_save_document_copy (LrgSaveDocument *self)
{
    LrgSaveDocument *copy;
    GHashTableIter iter;
    gpointer key;
    gpointer value;

    g_return_val_if_fail (self != NULL, NULL);

    copy = lrg_save_document_new (self->format_version);
    g_hash_table_iter_init (&iter, self->sections);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        Entry *entry = (Entry *) value;

        g_hash_table_insert (copy->sections, g_strdup ((const gchar *) key),
                             entry_new (entry->state, entry->version));
    }
    return copy;
}

guint
lrg_save_document_get_format_version (LrgSaveDocument *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->format_version;
}

void
lrg_save_document_set_format_version (LrgSaveDocument *self,
                                      guint            format_version)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (format_version > 0);

    self->format_version = format_version;
}

gboolean
lrg_save_document_has_section (LrgSaveDocument *self,
                               const gchar     *name)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_contains (self->sections, name);
}

GVariant *
lrg_save_document_get_section (LrgSaveDocument *self,
                               const gchar     *name)
{
    Entry *entry;

    g_return_val_if_fail (self != NULL, NULL);
    g_return_val_if_fail (name != NULL, NULL);

    entry = (Entry *) g_hash_table_lookup (self->sections, name);
    return entry != NULL ? entry->state : NULL;
}

guint
lrg_save_document_get_section_version (LrgSaveDocument *self,
                                       const gchar     *name)
{
    Entry *entry;

    g_return_val_if_fail (self != NULL, 0);
    g_return_val_if_fail (name != NULL, 0);

    entry = (Entry *) g_hash_table_lookup (self->sections, name);
    return entry != NULL ? entry->version : 0;
}

void
lrg_save_document_set_section (LrgSaveDocument *self,
                               const gchar     *name,
                               GVariant        *state,
                               guint            version)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (name != NULL && *name != '\0');
    g_return_if_fail (state != NULL);

    g_hash_table_replace (self->sections, g_strdup (name), entry_new (state, version));
}

gboolean
lrg_save_document_set_section_version (LrgSaveDocument *self,
                                       const gchar     *name,
                                       guint            version)
{
    Entry *entry;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    entry = (Entry *) g_hash_table_lookup (self->sections, name);
    if (entry == NULL)
        return FALSE;
    entry->version = version;
    return TRUE;
}

gboolean
lrg_save_document_remove_section (LrgSaveDocument *self,
                                  const gchar     *name)
{
    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (name != NULL, FALSE);

    return g_hash_table_remove (self->sections, name);
}

/*
 * lrg_save_document_rename_section:
 * Steals the entry from the table and inserts it under the new name, so
 * the state and version move unchanged.
 */
gboolean
lrg_save_document_rename_section (LrgSaveDocument  *self,
                                  const gchar      *old_name,
                                  const gchar      *new_name,
                                  GError          **error)
{
    gpointer old_key;
    gpointer value;

    g_return_val_if_fail (self != NULL, FALSE);
    g_return_val_if_fail (old_name != NULL, FALSE);
    g_return_val_if_fail (new_name != NULL && *new_name != '\0', FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (!g_hash_table_lookup_extended (self->sections, old_name, &old_key, &value))
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_NOT_FOUND,
                     "no section \"%s\" to rename", old_name);
        return FALSE;
    }
    if (strcmp (old_name, new_name) == 0)
        return TRUE;
    if (g_hash_table_contains (self->sections, new_name))
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_FAILED,
                     "cannot rename section \"%s\": \"%s\" exists", old_name, new_name);
        return FALSE;
    }
    g_hash_table_steal (self->sections, old_name);
    g_free (old_key);
    g_hash_table_insert (self->sections, g_strdup (new_name), value);
    return TRUE;
}

guint
lrg_save_document_get_n_sections (LrgSaveDocument *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return g_hash_table_size (self->sections);
}

/* byte order, so every listing and variant is stable */
static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
    return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

gchar **
lrg_save_document_get_section_names (LrgSaveDocument *self)
{
    gchar **names;
    guint length;

    g_return_val_if_fail (self != NULL, NULL);

    names = (gchar **) g_hash_table_get_keys_as_array (self->sections, &length);
    qsort (names, length, sizeof (gchar *), compare_names);
    /* the keys are the table's: copy them into an owned vector */
    {
        gchar **owned = g_new0 (gchar *, length + 1);
        guint i;

        for (i = 0; i < length; i++)
            owned[i] = g_strdup (names[i]);
        g_free (names);
        return owned;
    }
}

GVariant *
lrg_save_document_get_versions (LrgSaveDocument *self)
{
    g_auto(GStrv) names = NULL;
    GVariantBuilder builder;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    names = lrg_save_document_get_section_names (self);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{su}"));
    for (i = 0; names[i] != NULL; i++)
    {
        Entry *entry = (Entry *) g_hash_table_lookup (self->sections, names[i]);

        g_variant_builder_add (&builder, "{su}", names[i], (guint32) entry->version);
    }
    return g_variant_ref_sink (g_variant_builder_end (&builder));
}

GVariant *
lrg_save_document_to_variant (LrgSaveDocument *self)
{
    g_auto(GStrv) names = NULL;
    g_autoptr(GVariant) versions = NULL;
    GVariantBuilder sections;
    guint i;

    g_return_val_if_fail (self != NULL, NULL);

    names = lrg_save_document_get_section_names (self);
    g_variant_builder_init (&sections, G_VARIANT_TYPE_VARDICT);
    for (i = 0; names[i] != NULL; i++)
    {
        Entry *entry = (Entry *) g_hash_table_lookup (self->sections, names[i]);

        g_variant_builder_add (&sections, "{sv}", names[i], entry->state);
    }
    versions = lrg_save_document_get_versions (self);
    return g_variant_ref_sink (g_variant_new ("(u@a{sv}@a{su})", (guint32) self->format_version,
                                              g_variant_builder_end (&sections), versions));
}

gboolean
lrg_save_document_equal (LrgSaveDocument *a,
                         LrgSaveDocument *b)
{
    g_autoptr(GVariant) va = NULL;
    g_autoptr(GVariant) vb = NULL;

    g_return_val_if_fail (a != NULL, FALSE);
    g_return_val_if_fail (b != NULL, FALSE);

    va = lrg_save_document_to_variant (a);
    vb = lrg_save_document_to_variant (b);
    return g_variant_equal (va, vb);
}
