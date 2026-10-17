/* lrg-save-migrator.c
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Chained save conversion. A migration is one step N -> N + 1 of the
 * document or of one section; the migrator keeps them in two tables
 * (document steps by from version; section steps by section, then by
 * from version), so a chain is found by walking from the stored version
 * up one step at a time and a gap is noticed at the first missing step.
 *
 * Atomicity: lrg_save_migrator_migrate_document() converts a copy of the
 * document (states are immutable GVariants, so the copy is cheap) and
 * copies the result back only when every step succeeded. Signals are
 * emitted after that commit, so a listener never sees a step of a
 * conversion that was then thrown away.
 */

#include "lrg-save-migrator.h"

#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * LrgSaveMigration
 * ========================================================================== */

struct _LrgSaveMigration
{
    LrgSaveMigrationScope    scope;
    gchar                   *section;      /* NULL for a document step */
    guint                    from_version;
    gchar                   *description;
    LrgSaveDocumentStepFunc  document_func;
    LrgSaveSectionStepFunc   section_func;
    gpointer                 user_data;
    GDestroyNotify           destroy;
    GVariantType            *output_type;  /* nullable */
    gboolean                 added;        /* in a migrator: immutable */
};

static void
migration_clear (gpointer data)
{
    LrgSaveMigration *self = (LrgSaveMigration *) data;

    if (self->destroy != NULL)
        self->destroy (self->user_data);
    g_free (self->section);
    g_free (self->description);
    if (self->output_type != NULL)
        g_variant_type_free (self->output_type);
}

LrgSaveMigration *
lrg_save_migration_ref (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return (LrgSaveMigration *) g_atomic_rc_box_acquire (self);
}

void
lrg_save_migration_unref (LrgSaveMigration *self)
{
    g_return_if_fail (self != NULL);

    g_atomic_rc_box_release_full (self, migration_clear);
}

G_DEFINE_BOXED_TYPE (LrgSaveMigration, lrg_save_migration,
                     lrg_save_migration_ref, lrg_save_migration_unref)

/* the common part of both constructors */
static LrgSaveMigration *
migration_new (LrgSaveMigrationScope  scope,
               const gchar           *section,
               guint                  from_version,
               const gchar           *description,
               gpointer               user_data,
               GDestroyNotify         destroy)
{
    LrgSaveMigration *self = g_atomic_rc_box_new0 (LrgSaveMigration);

    self->scope = scope;
    self->section = g_strdup (section);
    self->from_version = from_version;
    self->description = g_strdup (description != NULL ? description : "");
    self->user_data = user_data;
    self->destroy = destroy;
    return self;
}

LrgSaveMigration *
lrg_save_migration_new_document (guint                   from_version,
                                 const gchar            *description,
                                 LrgSaveDocumentStepFunc func,
                                 gpointer                user_data,
                                 GDestroyNotify          destroy)
{
    LrgSaveMigration *self;

    g_return_val_if_fail (from_version > 0 && from_version < G_MAXUINT, NULL);
    g_return_val_if_fail (func != NULL, NULL);

    self = migration_new (LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL, from_version, description,
                          user_data, destroy);
    self->document_func = func;
    return self;
}

LrgSaveMigration *
lrg_save_migration_new_section (const gchar            *section,
                                guint                   from_version,
                                const gchar            *description,
                                LrgSaveSectionStepFunc  func,
                                gpointer                user_data,
                                GDestroyNotify          destroy)
{
    LrgSaveMigration *self;

    g_return_val_if_fail (section != NULL && *section != '\0', NULL);
    g_return_val_if_fail (from_version > 0 && from_version < G_MAXUINT, NULL);
    g_return_val_if_fail (func != NULL, NULL);

    self = migration_new (LRG_SAVE_MIGRATION_SCOPE_SECTION, section, from_version, description,
                          user_data, destroy);
    self->section_func = func;
    return self;
}

LrgSaveMigrationScope
lrg_save_migration_get_scope (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT);

    return self->scope;
}

const gchar *
lrg_save_migration_get_section (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->section;
}

guint
lrg_save_migration_get_from_version (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->from_version;
}

guint
lrg_save_migration_get_to_version (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, 0);

    return self->from_version + 1;
}

const gchar *
lrg_save_migration_get_description (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->description;
}

void
lrg_save_migration_set_output_type (LrgSaveMigration   *self,
                                    const GVariantType *type)
{
    g_return_if_fail (self != NULL);
    g_return_if_fail (!self->added);
    g_return_if_fail (type == NULL || g_variant_type_is_definite (type));

    if (self->output_type != NULL)
        g_variant_type_free (self->output_type);
    self->output_type = type != NULL ? g_variant_type_copy (type) : NULL;
}

const GVariantType *
lrg_save_migration_get_output_type (LrgSaveMigration *self)
{
    g_return_val_if_fail (self != NULL, NULL);

    return self->output_type;
}

gchar *
lrg_save_migration_to_string (LrgSaveMigration *self)
{
    g_autofree gchar *what = NULL;

    g_return_val_if_fail (self != NULL, NULL);

    what = self->scope == LRG_SAVE_MIGRATION_SCOPE_DOCUMENT ?
           g_strdup ("format") : g_strdup_printf ("section \"%s\"", self->section);
    if (*self->description == '\0')
        return g_strdup_printf ("%s %u -> %u", what, self->from_version, self->from_version + 1);
    return g_strdup_printf ("%s %u -> %u: %s", what, self->from_version, self->from_version + 1,
                            self->description);
}

/* ==========================================================================
 * LrgSaveMigrator
 * ========================================================================== */

struct _LrgSaveMigrator
{
    GObject     parent_instance;

    guint       current_format;
    GHashTable *section_versions;   /* name (owned) -> GUINT_TO_POINTER (version) */
    GHashTable *document_steps;     /* GUINT_TO_POINTER (from) -> LrgSaveMigration (owned) */
    GHashTable *section_steps;      /* name (owned) -> GHashTable (from -> migration, owned) */
};

G_DEFINE_FINAL_TYPE (LrgSaveMigrator, lrg_save_migrator, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_CURRENT_FORMAT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_STEP_APPLIED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
lrg_save_migrator_finalize (GObject *object)
{
    LrgSaveMigrator *self = LRG_SAVE_MIGRATOR (object);

    g_hash_table_unref (self->section_versions);
    g_hash_table_unref (self->document_steps);
    g_hash_table_unref (self->section_steps);
    G_OBJECT_CLASS (lrg_save_migrator_parent_class)->finalize (object);
}

static void
lrg_save_migrator_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    LrgSaveMigrator *self = LRG_SAVE_MIGRATOR (object);

    switch (prop_id)
    {
    case PROP_CURRENT_FORMAT:
        g_value_set_uint (value, self->current_format);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_save_migrator_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    LrgSaveMigrator *self = LRG_SAVE_MIGRATOR (object);

    switch (prop_id)
    {
    case PROP_CURRENT_FORMAT:
        lrg_save_migrator_set_current_format (self, g_value_get_uint (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
lrg_save_migrator_class_init (LrgSaveMigratorClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_save_migrator_finalize;
    object_class->get_property = lrg_save_migrator_get_property;
    object_class->set_property = lrg_save_migrator_set_property;

    /**
     * LrgSaveMigrator:current-format:
     *
     * The format version lrg_save_migrator_migrate_document() converts
     * documents to.
     */
    properties[PROP_CURRENT_FORMAT] =
        g_param_spec_uint ("current-format", "Current format",
                           "The format version documents are converted to",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgSaveMigrator::step-applied:
     * @self: the migrator
     * @migration: the step that was applied
     *
     * Emitted once for every step of a successful conversion, in the
     * order the steps ran, after the converted document or state was
     * committed.
     */
    signals[SIGNAL_STEP_APPLIED] =
        g_signal_new ("step-applied", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL, G_TYPE_NONE, 1,
                      LRG_TYPE_SAVE_MIGRATION | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
lrg_save_migrator_init (LrgSaveMigrator *self)
{
    self->current_format = 1;
    self->section_versions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->document_steps = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                                  (GDestroyNotify) lrg_save_migration_unref);
    self->section_steps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                 (GDestroyNotify) g_hash_table_unref);
}

LrgSaveMigrator *
lrg_save_migrator_new (void)
{
    return (LrgSaveMigrator *) g_object_new (LRG_TYPE_SAVE_MIGRATOR, NULL);
}

guint
lrg_save_migrator_get_current_format (LrgSaveMigrator *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), 0);

    return self->current_format;
}

void
lrg_save_migrator_set_current_format (LrgSaveMigrator *self,
                                      guint            format_version)
{
    g_return_if_fail (LRG_IS_SAVE_MIGRATOR (self));
    g_return_if_fail (format_version > 0);

    if (self->current_format == format_version)
        return;
    self->current_format = format_version;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CURRENT_FORMAT]);
}

void
lrg_save_migrator_set_section_version (LrgSaveMigrator *self,
                                       const gchar     *section,
                                       guint            version)
{
    g_return_if_fail (LRG_IS_SAVE_MIGRATOR (self));
    g_return_if_fail (section != NULL && *section != '\0');
    g_return_if_fail (version > 0);

    g_hash_table_replace (self->section_versions, g_strdup (section), GUINT_TO_POINTER (version));
}

guint
lrg_save_migrator_get_section_version (LrgSaveMigrator *self,
                                       const gchar     *section)
{
    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), 0);
    g_return_val_if_fail (section != NULL, 0);

    return GPOINTER_TO_UINT (g_hash_table_lookup (self->section_versions, section));
}

gboolean
lrg_save_migrator_forget_section (LrgSaveMigrator *self,
                                  const gchar     *section)
{
    gboolean known;
    gboolean stepped;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);
    g_return_val_if_fail (section != NULL, FALSE);

    known = g_hash_table_remove (self->section_versions, section);
    stepped = g_hash_table_remove (self->section_steps, section);
    return known || stepped;
}

/* byte order for every listing */
static gint
compare_names (gconstpointer a,
               gconstpointer b)
{
    return strcmp (*(const gchar * const *) a, *(const gchar * const *) b);
}

gchar **
lrg_save_migrator_get_sections (LrgSaveMigrator *self)
{
    gchar **keys;
    gchar **owned;
    guint length;
    guint i;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), NULL);

    keys = (gchar **) g_hash_table_get_keys_as_array (self->section_versions, &length);
    qsort (keys, length, sizeof (gchar *), compare_names);
    owned = g_new0 (gchar *, length + 1);
    for (i = 0; i < length; i++)
        owned[i] = g_strdup (keys[i]);
    g_free (keys);
    return owned;
}

/* the from -> migration table of a scope (NULL for a section without steps) */
static GHashTable *
steps_of (LrgSaveMigrator       *self,
          LrgSaveMigrationScope  scope,
          const gchar           *section)
{
    if (scope == LRG_SAVE_MIGRATION_SCOPE_DOCUMENT)
        return self->document_steps;
    if (section == NULL)
        return NULL;
    return (GHashTable *) g_hash_table_lookup (self->section_steps, section);
}

gboolean
lrg_save_migrator_add (LrgSaveMigrator   *self,
                       LrgSaveMigration  *migration,
                       GError           **error)
{
    GHashTable *table;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);
    g_return_val_if_fail (migration != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    table = steps_of (self, migration->scope, migration->section);
    if (table == NULL)
    {
        table = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
                                       (GDestroyNotify) lrg_save_migration_unref);
        g_hash_table_insert (self->section_steps, g_strdup (migration->section), table);
    }
    if (g_hash_table_contains (table, GUINT_TO_POINTER (migration->from_version)))
    {
        g_autofree gchar *what = lrg_save_migration_to_string (migration);

        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_DUPLICATE_STEP,
                     "there is already a conversion step for %s", what);
        return FALSE;
    }
    migration->added = TRUE;
    g_hash_table_insert (table, GUINT_TO_POINTER (migration->from_version),
                         lrg_save_migration_ref (migration));
    return TRUE;
}

gboolean
lrg_save_migrator_add_document_step (LrgSaveMigrator         *self,
                                     guint                    from_version,
                                     const gchar             *description,
                                     LrgSaveDocumentStepFunc  func,
                                     gpointer                 user_data,
                                     GDestroyNotify           destroy,
                                     GError                 **error)
{
    g_autoptr(LrgSaveMigration) migration = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);

    /* the step owns @user_data from here: a refused step frees it */
    migration = lrg_save_migration_new_document (from_version, description, func, user_data,
                                                 destroy);
    g_return_val_if_fail (migration != NULL, FALSE);
    return lrg_save_migrator_add (self, migration, error);
}

gboolean
lrg_save_migrator_add_section_step (LrgSaveMigrator         *self,
                                    const gchar             *section,
                                    guint                    from_version,
                                    const gchar             *description,
                                    LrgSaveSectionStepFunc   func,
                                    gpointer                 user_data,
                                    GDestroyNotify           destroy,
                                    GError                 **error)
{
    g_autoptr(LrgSaveMigration) migration = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);

    migration = lrg_save_migration_new_section (section, from_version, description, func,
                                                user_data, destroy);
    g_return_val_if_fail (migration != NULL, FALSE);
    return lrg_save_migrator_add (self, migration, error);
}

gboolean
lrg_save_migrator_remove (LrgSaveMigrator       *self,
                          LrgSaveMigrationScope  scope,
                          const gchar           *section,
                          guint                  from_version)
{
    GHashTable *table;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);

    table = steps_of (self, scope, section);
    return table != NULL && g_hash_table_remove (table, GUINT_TO_POINTER (from_version));
}

LrgSaveMigration *
lrg_save_migrator_lookup (LrgSaveMigrator       *self,
                          LrgSaveMigrationScope  scope,
                          const gchar           *section,
                          guint                  from_version)
{
    GHashTable *table;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), NULL);

    table = steps_of (self, scope, section);
    return table != NULL ?
           (LrgSaveMigration *) g_hash_table_lookup (table, GUINT_TO_POINTER (from_version)) :
           NULL;
}

guint
lrg_save_migrator_get_n_steps (LrgSaveMigrator *self)
{
    GHashTableIter iter;
    gpointer value;
    guint n;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), 0);

    n = g_hash_table_size (self->document_steps);
    g_hash_table_iter_init (&iter, self->section_steps);
    while (g_hash_table_iter_next (&iter, NULL, &value))
        n += g_hash_table_size ((GHashTable *) value);
    return n;
}

/* "format" or "section \"name\"" for messages */
static gchar *
describe_scope (LrgSaveMigrationScope  scope,
                const gchar           *section)
{
    if (scope == LRG_SAVE_MIGRATION_SCOPE_DOCUMENT)
        return g_strdup ("format");
    return g_strdup_printf ("section \"%s\"", section);
}

/*
 * lrg_save_migrator_plan:
 * Walks up from @from_version one step at a time; the first version
 * without a step is the gap the error names.
 */
GPtrArray *
lrg_save_migrator_plan (LrgSaveMigrator        *self,
                        LrgSaveMigrationScope   scope,
                        const gchar            *section,
                        guint                   from_version,
                        guint                   to_version,
                        GError                **error)
{
    g_autoptr(GPtrArray) chain = NULL;
    g_autofree gchar *what = NULL;
    GHashTable *table;
    guint version;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), NULL);
    g_return_val_if_fail (scope == LRG_SAVE_MIGRATION_SCOPE_DOCUMENT || section != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    what = describe_scope (scope, section);
    if (from_version == 0 || to_version == 0)
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_CORRUPT,
                     "%s has no version", what);
        return NULL;
    }
    if (from_version > to_version)
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH,
                     "%s is at version %u, newer than %u: it was written by a later "
                     "release and is never converted back", what, from_version, to_version);
        return NULL;
    }

    chain = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_save_migration_unref);
    table = steps_of (self, scope, section);
    for (version = from_version; version < to_version; version++)
    {
        LrgSaveMigration *step;

        step = table != NULL ?
               (LrgSaveMigration *) g_hash_table_lookup (table, GUINT_TO_POINTER (version)) :
               NULL;
        if (step == NULL)
        {
            g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_MISSING_STEP,
                         "no conversion step for %s from version %u to %u (needed to go from "
                         "%u to %u)", what, version, version + 1, from_version, to_version);
            return NULL;
        }
        g_ptr_array_add (chain, lrg_save_migration_ref (step));
    }
    return (GPtrArray *) g_steal_pointer (&chain);
}

/*
 * apply_section_step:
 * One section step on @state; the result is checked against the step's
 * output type. Any failure is LRG_SAVE_ERROR_STEP_FAILED naming the
 * step.
 */
static GVariant *
apply_section_step (LrgSaveMigration  *step,
                    GVariant          *state,
                    GError           **error)
{
    g_autoptr(GError) local = NULL;
    g_autofree gchar *what = NULL;
    GVariant *result;

    what = lrg_save_migration_to_string (step);
    result = step->section_func (state, step->user_data, &local);
    if (result == NULL)
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED,
                     "conversion step %s failed: %s", what,
                     local != NULL ? local->message : "it returned no state and no error");
        return NULL;
    }
    result = g_variant_take_ref (result);
    if (step->output_type != NULL && !g_variant_is_of_type (result, step->output_type))
    {
        g_autofree gchar *expected = g_variant_type_dup_string (step->output_type);

        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED,
                     "conversion step %s produced a %s state, expected %s", what,
                     g_variant_get_type_string (result), expected);
        g_variant_unref (result);
        return NULL;
    }
    return result;
}

/* runs @chain on @state; (transfer full) result or NULL */
static GVariant *
run_section_chain (GPtrArray  *chain,
                   GVariant   *state,
                   GError    **error)
{
    g_autoptr(GVariant) current = NULL;
    guint i;

    current = g_variant_ref (state);
    for (i = 0; i < chain->len; i++)
    {
        GVariant *next;

        next = apply_section_step ((LrgSaveMigration *) g_ptr_array_index (chain, i), current,
                                   error);
        if (next == NULL)
            return NULL;
        g_variant_unref (current);
        current = next;
    }
    return (GVariant *) g_steal_pointer (&current);
}

static void
emit_applied (LrgSaveMigrator *self,
              GPtrArray       *steps)
{
    guint i;

    for (i = 0; i < steps->len; i++)
        g_signal_emit (self, signals[SIGNAL_STEP_APPLIED], 0, g_ptr_array_index (steps, i));
}

GVariant *
lrg_save_migrator_migrate_section (LrgSaveMigrator  *self,
                                   const gchar      *section,
                                   GVariant         *state,
                                   guint             from_version,
                                   guint             to_version,
                                   GError          **error)
{
    g_autoptr(GPtrArray) chain = NULL;
    GVariant *result;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), NULL);
    g_return_val_if_fail (section != NULL, NULL);
    g_return_val_if_fail (state != NULL, NULL);
    g_return_val_if_fail (error == NULL || *error == NULL, NULL);

    chain = lrg_save_migrator_plan (self, LRG_SAVE_MIGRATION_SCOPE_SECTION, section, from_version,
                                    to_version, error);
    if (chain == NULL)
        return NULL;
    result = run_section_chain (chain, state, error);
    if (result != NULL)
        emit_applied (self, chain);
    return result;
}

/*
 * commit:
 * Makes @target hold exactly what @source holds (the target is the
 * caller's document, so its identity must stay).
 */
static void
commit (LrgSaveDocument *target,
        LrgSaveDocument *source)
{
    g_auto(GStrv) old_names = NULL;
    g_auto(GStrv) new_names = NULL;
    guint i;

    old_names = lrg_save_document_get_section_names (target);
    for (i = 0; old_names[i] != NULL; i++)
        lrg_save_document_remove_section (target, old_names[i]);
    new_names = lrg_save_document_get_section_names (source);
    for (i = 0; new_names[i] != NULL; i++)
        lrg_save_document_set_section (target, new_names[i],
                                       lrg_save_document_get_section (source, new_names[i]),
                                       lrg_save_document_get_section_version (source,
                                                                              new_names[i]));
    lrg_save_document_set_format_version (target, lrg_save_document_get_format_version (source));
}

/*
 * lrg_save_migrator_migrate_document:
 * 1. the format: refuse a newer one, run the document chain on a copy;
 * 2. every known section of the converted copy, in name order: refuse
 *    an unversioned or newer one, run its chain;
 * 3. commit, report, signal.
 */
gboolean
lrg_save_migrator_migrate_document (LrgSaveMigrator  *self,
                                    LrgSaveDocument  *document,
                                    GPtrArray       **applied,
                                    GError          **error)
{
    g_autoptr(LrgSaveDocument) work = NULL;
    g_autoptr(GPtrArray) chain = NULL;
    g_autoptr(GPtrArray) done = NULL;
    g_auto(GStrv) names = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);
    g_return_val_if_fail (document != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    if (applied != NULL)
        *applied = NULL;
    done = g_ptr_array_new_with_free_func ((GDestroyNotify) lrg_save_migration_unref);

    /* --- 1. the document steps, on a working copy --- */
    chain = lrg_save_migrator_plan (self, LRG_SAVE_MIGRATION_SCOPE_DOCUMENT, NULL,
                                    lrg_save_document_get_format_version (document),
                                    self->current_format, error);
    if (chain == NULL)
        return FALSE;
    work = lrg_save_document_copy (document);
    for (i = 0; i < chain->len; i++)
    {
        LrgSaveMigration *step = (LrgSaveMigration *) g_ptr_array_index (chain, i);
        g_autoptr(GError) local = NULL;

        if (!step->document_func (work, step->user_data, &local))
        {
            g_autofree gchar *what = lrg_save_migration_to_string (step);

            g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_STEP_FAILED,
                         "conversion step %s failed: %s", what,
                         local != NULL ? local->message : "it reported no error");
            return FALSE;
        }
        lrg_save_document_set_format_version (work, step->from_version + 1);
        g_ptr_array_add (done, lrg_save_migration_ref (step));
    }

    /* --- 2. every known section --- */
    names = lrg_save_document_get_section_names (work);
    for (i = 0; names[i] != NULL; i++)
    {
        g_autoptr(GPtrArray) section_chain = NULL;
        g_autoptr(GVariant) converted = NULL;
        guint current;
        guint stored;
        guint j;

        current = lrg_save_migrator_get_section_version (self, names[i]);
        if (current == 0)
            continue;    /* unknown: kept as it is */
        stored = lrg_save_document_get_section_version (work, names[i]);
        section_chain = lrg_save_migrator_plan (self, LRG_SAVE_MIGRATION_SCOPE_SECTION, names[i],
                                                stored, current, error);
        if (section_chain == NULL)
            return FALSE;
        if (section_chain->len == 0)
            continue;
        converted = run_section_chain (section_chain, lrg_save_document_get_section (work, names[i]),
                                       error);
        if (converted == NULL)
            return FALSE;
        lrg_save_document_set_section (work, names[i], converted, current);
        for (j = 0; j < section_chain->len; j++)
            g_ptr_array_add (done, lrg_save_migration_ref (g_ptr_array_index (section_chain, j)));
    }

    /* --- 3. commit --- */
    commit (document, work);
    emit_applied (self, done);
    if (applied != NULL)
        *applied = (GPtrArray *) g_steal_pointer (&done);
    return TRUE;
}

gboolean
lrg_save_migrator_needs_migration (LrgSaveMigrator *self,
                                   LrgSaveDocument *document)
{
    g_auto(GStrv) names = NULL;
    guint i;

    g_return_val_if_fail (LRG_IS_SAVE_MIGRATOR (self), FALSE);
    g_return_val_if_fail (document != NULL, FALSE);

    if (lrg_save_document_get_format_version (document) != self->current_format)
        return TRUE;
    names = lrg_save_document_get_section_names (document);
    for (i = 0; names[i] != NULL; i++)
    {
        guint current = lrg_save_migrator_get_section_version (self, names[i]);

        if (current != 0 && lrg_save_document_get_section_version (document, names[i]) != current)
            return TRUE;
    }
    return FALSE;
}
