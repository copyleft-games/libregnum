/* lrg-save-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Implementation of the save manager.
 */

#define LRG_LOG_DOMAIN LRG_LOG_DOMAIN_SAVE

#include "lrg-save-manager.h"
#include "lrg-save-context-private.h"
#include "../lrg-log.h"
#include <gio/gio.h>

/*
 * Background writer state, shared by the manager, its queued jobs and the
 * completion sources it posts to the caller's main context. It is
 * reference counted (atomically) so a completion source that runs after
 * the manager is gone finds valid memory; it reaches the manager only
 * through a weak reference.
 */
typedef struct
{
    gint          ref_count;
    GMutex        lock;
    GCond         cond;
    guint         pending;      /* jobs queued or being written */
    GQueue       *finished;     /* SaveJob *: written, not yet reported */
    GError       *first_error;  /* first failed write since the last flush */
    GWeakRef      manager;
    GMainContext *context;      /* where save-completed is emitted */
} SaveWriter;

/* One background save: the finished document and where it goes. */
typedef struct
{
    SaveWriter   *writer;
    gchar        *slot_name;
    gchar        *path;
    YamlDocument *document;
    gboolean      success;
} SaveJob;

struct _LrgSaveManager
{
    GObject parent_instance;

    gchar *save_directory;
    guint  save_version;

    /* Registered saveables: save_id -> LrgSaveable* */
    GHashTable *saveables;

    /* Background saves: one writer thread (created on first use) */
    SaveWriter  *writer;
    GThreadPool *write_pool;
};

G_DEFINE_FINAL_TYPE (LrgSaveManager, lrg_save_manager, G_TYPE_OBJECT)

/* Singleton instance */
static LrgSaveManager *default_manager = NULL;

enum
{
    PROP_0,
    PROP_SAVE_DIRECTORY,
    PROP_SAVE_VERSION,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_SAVE_STARTED,
    SIGNAL_SAVE_COMPLETED,
    SIGNAL_LOAD_STARTED,
    SIGNAL_LOAD_COMPLETED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

/* ==========================================================================
 * Private Helpers
 * ========================================================================== */

static gchar *
get_slot_path (LrgSaveManager *self,
               const gchar    *slot_name)
{
    g_autofree gchar *filename = g_strdup_printf ("%s.yaml", slot_name);

    /* g_build_filename copies its arguments, so the printf'd filename must be
     * freed by us rather than leaked. */
    return g_build_filename (self->save_directory, filename, NULL);
}

static void
ensure_directory_exists (const gchar *directory)
{
    if (!g_file_test (directory, G_FILE_TEST_IS_DIR))
    {
        g_mkdir_with_parents (directory, 0755);
        lrg_log_debug ("Created save directory: %s", directory);
    }
}

/* ==========================================================================
 * Background Writer
 *
 * A save has two halves. Collecting the state (each saveable's save
 * method) must run on the thread that owns the game state, but generating
 * the YAML text and writing the file (g_file_set_contents() fsyncs when it
 * replaces a file: several milliseconds on a laptop SSD, far more on a
 * busy or spinning disk) need not. lrg_save_manager_save_in_background()
 * collects on the caller's thread and hands the rest to a single writer
 * thread, so saves land in the order they were made.
 * ========================================================================== */

static SaveWriter *
save_writer_new (LrgSaveManager *manager)
{
    SaveWriter *writer;

    writer = g_new0 (SaveWriter, 1);
    writer->ref_count = 1;
    g_mutex_init (&writer->lock);
    g_cond_init (&writer->cond);
    writer->finished = g_queue_new ();
    g_weak_ref_init (&writer->manager, manager);
    writer->context = g_main_context_ref_thread_default ();
    return writer;
}

static void
save_job_free (SaveJob *job)
{
    g_clear_pointer (&job->document, g_object_unref);
    g_free (job->slot_name);
    g_free (job->path);
    g_free (job);
}

static SaveWriter *
save_writer_ref (SaveWriter *writer)
{
    g_atomic_int_inc (&writer->ref_count);
    return writer;
}

static void
save_writer_unref (SaveWriter *writer)
{
    if (!g_atomic_int_dec_and_test (&writer->ref_count))
        return;

    g_queue_free_full (writer->finished, (GDestroyNotify) save_job_free);
    g_clear_error (&writer->first_error);
    g_weak_ref_clear (&writer->manager);
    g_main_context_unref (writer->context);
    g_cond_clear (&writer->cond);
    g_mutex_clear (&writer->lock);
    g_free (writer);
}

/*
 * Emits save-completed for every finished background save, oldest first,
 * on the calling thread. Called from the completion source, from
 * lrg_save_manager_flush() and before every synchronous operation.
 */
static void
save_writer_report (SaveWriter *writer)
{
    g_autoptr(LrgSaveManager) manager = NULL;
    GQueue                     done = G_QUEUE_INIT;
    SaveJob                   *job;

    g_mutex_lock (&writer->lock);
    while ((job = g_queue_pop_head (writer->finished)) != NULL)
        g_queue_push_tail (&done, job);
    g_mutex_unlock (&writer->lock);

    manager = g_weak_ref_get (&writer->manager);
    while ((job = g_queue_pop_head (&done)) != NULL)
    {
        if (manager != NULL)
        {
            if (job->success)
                lrg_log_info ("Saved successfully to: %s", job->slot_name);
            g_signal_emit (manager, signals[SIGNAL_SAVE_COMPLETED], 0,
                           job->slot_name, job->success);
        }
        save_job_free (job);
    }
}

static gboolean
save_writer_report_source (gpointer user_data)
{
    save_writer_report (user_data);
    return G_SOURCE_REMOVE;
}

/* Writer thread: generate the text and write the file. */
static void
save_writer_run (gpointer data,
                 gpointer user_data)
{
    SaveJob           *job = data;
    SaveWriter        *writer = job->writer;
    g_autoptr(GError)  error = NULL;
    g_autofree gchar  *yaml_str = NULL;
    GSource           *source;

    yaml_str = lrg_save_context_document_to_string (job->document, &error);
    g_clear_pointer (&job->document, g_object_unref);
    job->success = yaml_str != NULL &&
                   g_file_set_contents (job->path, yaml_str, -1, &error);
    if (!job->success)
        lrg_log_warning ("Background save to %s failed: %s", job->path,
                         error != NULL ? error->message : "unknown error");

    g_mutex_lock (&writer->lock);
    if (!job->success && writer->first_error == NULL && error != NULL)
        writer->first_error = g_steal_pointer (&error);
    g_queue_push_tail (writer->finished, job);
    writer->pending--;
    g_cond_broadcast (&writer->cond);
    g_mutex_unlock (&writer->lock);

    /* Report on the saving thread's main context, if it runs one; the
     * manager also reports on its next flush or synchronous call. */
    source = g_idle_source_new ();
    g_source_set_priority (source, G_PRIORITY_DEFAULT);
    g_source_set_callback (source, save_writer_report_source,
                           save_writer_ref (writer),
                           (GDestroyNotify) save_writer_unref);
    g_source_set_static_name (source, "[libregnum] save-completed");
    g_source_attach (source, writer->context);
    g_source_unref (source);
    save_writer_unref (writer);
}

/* Blocks until every queued background save is written; returns (and
 * clears) the first write error since the last call when @error is set. */
static gboolean
save_writer_wait (LrgSaveManager  *self,
                  GError         **error)
{
    SaveWriter *writer = self->writer;
    GError     *first;

    if (writer == NULL)
        return TRUE;

    g_mutex_lock (&writer->lock);
    while (writer->pending > 0)
        g_cond_wait (&writer->cond, &writer->lock);
    first = NULL;
    if (error != NULL)
    {
        first = writer->first_error;
        writer->first_error = NULL;
    }
    g_mutex_unlock (&writer->lock);

    save_writer_report (writer);
    if (first != NULL)
    {
        g_propagate_error (error, first);
        return FALSE;
    }
    return TRUE;
}

static void
lrg_save_manager_finalize (GObject *object)
{
    LrgSaveManager *self = LRG_SAVE_MANAGER (object);

    /* Finish queued background saves (no completion signals: the manager
     * is going away), then drop the writer. */
    if (self->write_pool != NULL)
        g_thread_pool_free (self->write_pool, FALSE, TRUE);
    self->write_pool = NULL;
    if (self->writer != NULL)
    {
        g_weak_ref_set (&self->writer->manager, NULL);
        g_clear_pointer (&self->writer, save_writer_unref);
    }

    g_clear_pointer (&self->save_directory, g_free);
    g_clear_pointer (&self->saveables, g_hash_table_unref);

    if (default_manager == self)
        default_manager = NULL;

    G_OBJECT_CLASS (lrg_save_manager_parent_class)->finalize (object);
}

static void
lrg_save_manager_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    LrgSaveManager *self = LRG_SAVE_MANAGER (object);

    switch (prop_id)
    {
    case PROP_SAVE_DIRECTORY:
        g_value_set_string (value, self->save_directory);
        break;
    case PROP_SAVE_VERSION:
        g_value_set_uint (value, self->save_version);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_save_manager_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    LrgSaveManager *self = LRG_SAVE_MANAGER (object);

    switch (prop_id)
    {
    case PROP_SAVE_DIRECTORY:
        g_free (self->save_directory);
        self->save_directory = g_value_dup_string (value);
        break;
    case PROP_SAVE_VERSION:
        self->save_version = g_value_get_uint (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_save_manager_class_init (LrgSaveManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_save_manager_finalize;
    object_class->get_property = lrg_save_manager_get_property;
    object_class->set_property = lrg_save_manager_set_property;

    /**
     * LrgSaveManager:save-directory:
     *
     * The directory where save files are stored.
     */
    properties[PROP_SAVE_DIRECTORY] =
        g_param_spec_string ("save-directory",
                             "Save Directory",
                             "Directory for save files",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgSaveManager:save-version:
     *
     * The current save format version.
     */
    properties[PROP_SAVE_VERSION] =
        g_param_spec_uint ("save-version",
                           "Save Version",
                           "Current save format version",
                           1, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /*
     * LrgSaveManager::save-started:
     * @self: the #LrgSaveManager
     * @slot_name: the slot being saved to
     *
     * Emitted when a save operation begins.
     */
    signals[SIGNAL_SAVE_STARTED] =
        g_signal_new ("save-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);

    /*
     * LrgSaveManager::save-completed:
     * @self: the #LrgSaveManager
     * @slot_name: the slot that was saved
     * @success: whether the save succeeded
     *
     * Emitted when a save operation completes.
     */
    signals[SIGNAL_SAVE_COMPLETED] =
        g_signal_new ("save-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_BOOLEAN);

    /*
     * LrgSaveManager::load-started:
     * @self: the #LrgSaveManager
     * @slot_name: the slot being loaded from
     *
     * Emitted when a load operation begins.
     */
    signals[SIGNAL_LOAD_STARTED] =
        g_signal_new ("load-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 1,
                      G_TYPE_STRING);

    /*
     * LrgSaveManager::load-completed:
     * @self: the #LrgSaveManager
     * @slot_name: the slot that was loaded
     * @success: whether the load succeeded
     *
     * Emitted when a load operation completes.
     */
    signals[SIGNAL_LOAD_COMPLETED] =
        g_signal_new ("load-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0,
                      NULL, NULL,
                      NULL,
                      G_TYPE_NONE, 2,
                      G_TYPE_STRING, G_TYPE_BOOLEAN);
}

static void
lrg_save_manager_init (LrgSaveManager *self)
{
    g_autofree gchar *default_dir = NULL;

    /* Default save directory in user data dir */
    default_dir = g_build_filename (g_get_user_data_dir (),
                                    "libregnum", "saves",
                                    NULL);
    self->save_directory = g_strdup (default_dir);
    self->save_version = 1;
    self->saveables = g_hash_table_new_full (g_str_hash, g_str_equal,
                                              g_free, g_object_unref);
}

/* ==========================================================================
 * Construction and Singleton
 * ========================================================================== */

/*
 * lrg_save_manager_get_default:
 *
 * Gets the default save manager instance.
 *
 * Returns: (transfer none): The default #LrgSaveManager
 */
LrgSaveManager *
lrg_save_manager_get_default (void)
{
    if (default_manager == NULL)
    {
        default_manager = lrg_save_manager_new ();
    }

    return default_manager;
}

/*
 * lrg_save_manager_new:
 *
 * Creates a new save manager.
 *
 * Returns: (transfer full): A new #LrgSaveManager
 */
LrgSaveManager *
lrg_save_manager_new (void)
{
    return g_object_new (LRG_TYPE_SAVE_MANAGER, NULL);
}

/* ==========================================================================
 * Configuration
 * ========================================================================== */

/*
 * lrg_save_manager_get_save_directory:
 * @self: a #LrgSaveManager
 *
 * Gets the directory where save files are stored.
 *
 * Returns: (transfer none): the save directory path
 */
const gchar *
lrg_save_manager_get_save_directory (LrgSaveManager *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), NULL);

    return self->save_directory;
}

/*
 * lrg_save_manager_set_save_directory:
 * @self: a #LrgSaveManager
 * @directory: the save directory path
 *
 * Sets the directory where save files are stored.
 */
void
lrg_save_manager_set_save_directory (LrgSaveManager *self,
                                     const gchar    *directory)
{
    g_return_if_fail (LRG_IS_SAVE_MANAGER (self));
    g_return_if_fail (directory != NULL);

    if (g_strcmp0 (self->save_directory, directory) == 0)
        return;

    g_free (self->save_directory);
    self->save_directory = g_strdup (directory);

    ensure_directory_exists (self->save_directory);

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SAVE_DIRECTORY]);

    lrg_log_info ("Save directory set to: %s", directory);
}

/*
 * lrg_save_manager_get_save_version:
 * @self: a #LrgSaveManager
 *
 * Gets the current save format version.
 *
 * Returns: the version number
 */
guint
lrg_save_manager_get_save_version (LrgSaveManager *self)
{
    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), 0);

    return self->save_version;
}

/*
 * lrg_save_manager_set_save_version:
 * @self: a #LrgSaveManager
 * @version: the version number
 *
 * Sets the current save format version.
 */
void
lrg_save_manager_set_save_version (LrgSaveManager *self,
                                   guint           version)
{
    g_return_if_fail (LRG_IS_SAVE_MANAGER (self));

    g_return_if_fail (version > 0);

    if (self->save_version == version)
        return;

    self->save_version = version;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SAVE_VERSION]);
}

/* ==========================================================================
 * Saveable Registration
 * ========================================================================== */

/*
 * lrg_save_manager_register:
 * @self: a #LrgSaveManager
 * @saveable: the #LrgSaveable to register
 *
 * Registers a saveable object with the manager.
 */
void
lrg_save_manager_register (LrgSaveManager *self,
                           LrgSaveable    *saveable)
{
    const gchar *save_id;

    g_return_if_fail (LRG_IS_SAVE_MANAGER (self));
    g_return_if_fail (LRG_IS_SAVEABLE (saveable));

    save_id = lrg_saveable_get_save_id (saveable);
    g_return_if_fail (save_id != NULL);

    g_hash_table_insert (self->saveables,
                         g_strdup (save_id),
                         g_object_ref (saveable));

    lrg_log_debug ("Registered saveable: %s", save_id);
}

/*
 * lrg_save_manager_unregister:
 * @self: a #LrgSaveManager
 * @saveable: the #LrgSaveable to unregister
 *
 * Unregisters a saveable object from the manager.
 */
void
lrg_save_manager_unregister (LrgSaveManager *self,
                             LrgSaveable    *saveable)
{
    const gchar *save_id;

    g_return_if_fail (LRG_IS_SAVE_MANAGER (self));
    g_return_if_fail (LRG_IS_SAVEABLE (saveable));

    save_id = lrg_saveable_get_save_id (saveable);
    g_return_if_fail (save_id != NULL);

    if (g_hash_table_remove (self->saveables, save_id))
    {
        lrg_log_debug ("Unregistered saveable: %s", save_id);
    }
}

/*
 * lrg_save_manager_unregister_all:
 * @self: a #LrgSaveManager
 *
 * Unregisters all saveable objects.
 */
void
lrg_save_manager_unregister_all (LrgSaveManager *self)
{
    g_return_if_fail (LRG_IS_SAVE_MANAGER (self));

    g_hash_table_remove_all (self->saveables);

    lrg_log_debug ("Unregistered all saveables");
}

/* ==========================================================================
 * Synchronous Save/Load
 * ========================================================================== */

/*
 * lrg_save_manager_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: return location for a #GError
 *
 * Saves the game state to the specified slot.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_save_manager_save (LrgSaveManager *self,
                       const gchar *slot_name,
                       GError **error)
{
    return lrg_save_manager_save_with_description (self, slot_name, NULL, error);
}

/*
 * save_collect:
 * Runs every registered saveable into a fresh context on the calling
 * thread and finishes it into a document. Emits save-started. On failure
 * returns %NULL with @error set and leaves save-completed to the caller.
 */
static YamlDocument *
save_collect (LrgSaveManager  *self,
              const gchar     *slot_name,
              const gchar     *description,
              GError         **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    GHashTableIter             iter;
    gpointer                   key;
    gpointer                   value;

    ensure_directory_exists (self->save_directory);

    g_signal_emit (self, signals[SIGNAL_SAVE_STARTED], 0, slot_name);

    lrg_log_info ("Saving to slot: %s", slot_name);

    context = lrg_save_context_new_for_save ();
    lrg_save_context_set_version (context, self->save_version);

    /* Write metadata section */
    lrg_save_context_begin_section (context, "metadata");
    lrg_save_context_write_string (context, "slot_name", slot_name);
    if (description != NULL)
        lrg_save_context_write_string (context, "display_name", description);
    {
        g_autoptr(GDateTime) now = g_date_time_new_now_utc ();
        g_autofree gchar *timestamp = g_date_time_format_iso8601 (now);
        lrg_save_context_write_string (context, "timestamp", timestamp);
    }
    lrg_save_context_end_section (context);

    /* Save each registered saveable */
    g_hash_table_iter_init (&iter, self->saveables);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *save_id = (const gchar *) key;
        LrgSaveable *saveable = LRG_SAVEABLE (value);

        lrg_save_context_begin_section (context, save_id);

        if (!lrg_saveable_save (saveable, context, error))
        {
            lrg_log_error ("Failed to save object: %s", save_id);
            return NULL;
        }

        lrg_save_context_end_section (context);
    }

    return lrg_save_context_take_document (context, error);
}

gboolean
lrg_save_manager_save_with_description (LrgSaveManager *self,
                                        const gchar *slot_name,
                                        const gchar *description,
                                        GError **error)
{
    g_autoptr(YamlDocument) doc = NULL;
    g_autofree gchar       *path = NULL;
    g_autofree gchar       *yaml_str = NULL;
    gboolean                success = FALSE;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (slot_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* An earlier background save of this slot must not land after this one. */
    save_writer_wait (self, NULL);

    doc = save_collect (self, slot_name, description, error);
    if (doc != NULL)
    {
        path = get_slot_path (self, slot_name);
        yaml_str = lrg_save_context_document_to_string (doc, error);
        success = yaml_str != NULL &&
                  g_file_set_contents (path, yaml_str, -1, error);
        if (success)
            lrg_log_info ("Saved context to %s", path);
    }

    g_signal_emit (self, signals[SIGNAL_SAVE_COMPLETED], 0, slot_name, success);

    if (success)
    {
        lrg_log_info ("Saved successfully to: %s", slot_name);
    }

    return success;
}

/*
 * lrg_save_manager_save_in_background:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @description: (nullable): the user-visible save description
 * @error: return location for a #GError
 *
 * Collects the state on this thread and writes it on the writer thread.
 *
 * Returns: %TRUE if the save was collected and queued
 */
gboolean
lrg_save_manager_save_in_background (LrgSaveManager  *self,
                                     const gchar     *slot_name,
                                     const gchar     *description,
                                     GError         **error)
{
    g_autoptr(YamlDocument) doc = NULL;
    SaveJob                *job;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (slot_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    doc = save_collect (self, slot_name, description, error);
    if (doc == NULL)
    {
        g_signal_emit (self, signals[SIGNAL_SAVE_COMPLETED], 0, slot_name, FALSE);
        return FALSE;
    }

    /* One writer thread per manager keeps the saves in order. */
    if (self->writer == NULL)
        self->writer = save_writer_new (self);
    if (self->write_pool == NULL)
    {
        self->write_pool = g_thread_pool_new (save_writer_run, NULL, 1, FALSE, error);
        if (self->write_pool == NULL)
        {
            g_signal_emit (self, signals[SIGNAL_SAVE_COMPLETED], 0, slot_name, FALSE);
            return FALSE;
        }
    }

    job = g_new0 (SaveJob, 1);
    job->writer = save_writer_ref (self->writer);
    job->slot_name = g_strdup (slot_name);
    job->path = get_slot_path (self, slot_name);
    job->document = g_steal_pointer (&doc);

    g_mutex_lock (&self->writer->lock);
    self->writer->pending++;
    g_mutex_unlock (&self->writer->lock);

    /* Cannot fail: the pool exists and runs on non-exclusive threads. */
    g_thread_pool_push (self->write_pool, job, NULL);
    return TRUE;
}

/*
 * lrg_save_manager_flush:
 * @self: a #LrgSaveManager
 * @error: return location for a #GError
 *
 * Waits for queued background saves and reports them.
 *
 * Returns: %TRUE if every background save since the last flush was written
 */
gboolean
lrg_save_manager_flush (LrgSaveManager  *self,
                        GError         **error)
{
    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return save_writer_wait (self, error);
}

/*
 * lrg_save_manager_is_saving:
 * @self: a #LrgSaveManager
 *
 * Returns: %TRUE while a background save is queued or being written
 */
gboolean
lrg_save_manager_is_saving (LrgSaveManager *self)
{
    gboolean saving;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);

    if (self->writer == NULL)
        return FALSE;
    g_mutex_lock (&self->writer->lock);
    saving = self->writer->pending > 0;
    g_mutex_unlock (&self->writer->lock);
    return saving;
}

/*
 * lrg_save_manager_load:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: return location for a #GError
 *
 * Loads the game state from the specified slot.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_save_manager_load (LrgSaveManager  *self,
                       const gchar     *slot_name,
                       GError         **error)
{
    g_autoptr(LrgSaveContext) context = NULL;
    g_autofree gchar          *path = NULL;
    GHashTableIter             iter;
    gpointer                   key;
    gpointer                   value;
    gboolean                   success = TRUE;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (slot_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* Read what a queued background save is about to write, not the old file. */
    save_writer_wait (self, NULL);

    path = get_slot_path (self, slot_name);

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_NOT_FOUND,
                     "Save slot not found: %s", slot_name);
        return FALSE;
    }

    g_signal_emit (self, signals[SIGNAL_LOAD_STARTED], 0, slot_name);

    lrg_log_info ("Loading from slot: %s", slot_name);

    context = lrg_save_context_new_from_file (path, error);
    if (context == NULL)
    {
        g_signal_emit (self, signals[SIGNAL_LOAD_COMPLETED], 0, slot_name, FALSE);
        return FALSE;
    }

    /* Compatibility must be checked before any game state is modified. */
    if (lrg_save_context_get_version (context) > self->save_version)
    {
        g_set_error (error, LRG_SAVE_ERROR, LRG_SAVE_ERROR_VERSION_MISMATCH,
                     "Save version %u is newer than supported version %u",
                     lrg_save_context_get_version (context), self->save_version);
        g_signal_emit (self, signals[SIGNAL_LOAD_COMPLETED], 0, slot_name, FALSE);
        return FALSE;
    }

    /* Load each registered saveable */
    g_hash_table_iter_init (&iter, self->saveables);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *save_id = (const gchar *) key;
        LrgSaveable *saveable = LRG_SAVEABLE (value);

        if (!lrg_save_context_has_section (context, save_id))
        {
            lrg_log_warning ("No saved data for: %s", save_id);
            continue;
        }

        if (!lrg_save_context_enter_section (context, save_id))
        {
            continue;
        }

        if (!lrg_saveable_load (saveable, context, error))
        {
            lrg_log_error ("Failed to load object: %s", save_id);
            success = FALSE;
            lrg_save_context_leave_section (context);
            break;
        }

        lrg_save_context_leave_section (context);
    }

    g_signal_emit (self, signals[SIGNAL_LOAD_COMPLETED], 0, slot_name, success);

    if (success)
    {
        lrg_log_info ("Loaded successfully from: %s", slot_name);
    }

    return success;
}

#ifdef LRG_HAS_LIBDEX
/* ==========================================================================
 * Asynchronous Save/Load
 * ========================================================================== */

typedef struct
{
    LrgSaveManager *manager;
    gchar          *slot_name;
} AsyncSaveLoadData;

static void
async_save_load_data_free (gpointer data)
{
    AsyncSaveLoadData *async_data = data;

    g_object_unref (async_data->manager);
    g_free (async_data->slot_name);
    g_free (async_data);
}

static DexFuture *
save_fiber (gpointer user_data)
{
    AsyncSaveLoadData  *data = user_data;
    g_autoptr(GError)   error = NULL;
    gboolean            result;

    result = lrg_save_manager_save (data->manager, data->slot_name, &error);

    if (error != NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_boolean (result);
}

static DexFuture *
load_fiber (gpointer user_data)
{
    AsyncSaveLoadData  *data = user_data;
    g_autoptr(GError)   error = NULL;
    gboolean            result;

    result = lrg_save_manager_load (data->manager, data->slot_name, &error);

    if (error != NULL)
    {
        return dex_future_new_for_error (g_steal_pointer (&error));
    }

    return dex_future_new_for_boolean (result);
}

/**
 * lrg_save_manager_save_async:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Saves the game state asynchronously.
 *
 * Returns: (transfer full): A #DexFuture
 */
DexFuture *
lrg_save_manager_save_async (LrgSaveManager *self,
                             const gchar    *slot_name)
{
    AsyncSaveLoadData *data;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), NULL);
    g_return_val_if_fail (slot_name != NULL, NULL);

    data = g_new0 (AsyncSaveLoadData, 1);
    data->manager = g_object_ref (self);
    data->slot_name = g_strdup (slot_name);

    return dex_scheduler_spawn (NULL, 0,
                                save_fiber,
                                data,
                                async_save_load_data_free);
}

/**
 * lrg_save_manager_load_async:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Loads the game state asynchronously.
 *
 * Returns: (transfer full): A #DexFuture
 */
DexFuture *
lrg_save_manager_load_async (LrgSaveManager *self,
                             const gchar    *slot_name)
{
    AsyncSaveLoadData *data;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), NULL);
    g_return_val_if_fail (slot_name != NULL, NULL);

    data = g_new0 (AsyncSaveLoadData, 1);
    data->manager = g_object_ref (self);
    data->slot_name = g_strdup (slot_name);

    return dex_scheduler_spawn (NULL, 0,
                                load_fiber,
                                data,
                                async_save_load_data_free);
}
#endif /* LRG_HAS_LIBDEX */

/* ==========================================================================
 * Save Slot Management
 * ========================================================================== */

/*
 * lrg_save_manager_list_saves:
 * @self: a #LrgSaveManager
 *
 * Lists all available save games.
 *
 * Returns: (transfer full) (element-type LrgSaveGame): A list of save games
 */
GList *
lrg_save_manager_list_saves (LrgSaveManager *self)
{
    g_autoptr(GDir) dir = NULL;
    GList           *saves = NULL;
    const gchar     *filename;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), NULL);

    if (!g_file_test (self->save_directory, G_FILE_TEST_IS_DIR))
    {
        return NULL;
    }

    dir = g_dir_open (self->save_directory, 0, NULL);
    if (dir == NULL)
    {
        return NULL;
    }

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        g_autofree gchar *path = NULL;
        LrgSaveGame      *save;

        if (!g_str_has_suffix (filename, ".yaml") &&
            !g_str_has_suffix (filename, ".yml"))
        {
            continue;
        }

        path = g_build_filename (self->save_directory, filename, NULL);
        {
            g_autoptr(GError) metadata_error = NULL;
            save = lrg_save_game_new_from_file (path, &metadata_error);
        }

        if (save != NULL)
        {
            saves = g_list_prepend (saves, save);
        }
    }

    return g_list_reverse (saves);
}

/*
 * lrg_save_manager_get_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Gets the save game for a specific slot.
 *
 * Returns: (transfer full) (nullable): The #LrgSaveGame
 */
LrgSaveGame *
lrg_save_manager_get_save (LrgSaveManager *self,
                           const gchar    *slot_name)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), NULL);
    g_return_val_if_fail (slot_name != NULL, NULL);

    path = get_slot_path (self, slot_name);

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        return NULL;
    }

    {
        g_autoptr(GError) metadata_error = NULL;
        return lrg_save_game_new_from_file (path, &metadata_error);
    }
}

/*
 * lrg_save_manager_delete_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: return location for a #GError
 *
 * Deletes a save game.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_save_manager_delete_save (LrgSaveManager  *self,
                              const gchar     *slot_name,
                              GError         **error)
{
    g_autofree gchar   *path = NULL;
    g_autoptr(GFile)    file = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (slot_name != NULL, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    /* A queued background save must not recreate the slot after this. */
    save_writer_wait (self, NULL);

    path = get_slot_path (self, slot_name);

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_set_error (error,
                     LRG_SAVE_ERROR,
                     LRG_SAVE_ERROR_NOT_FOUND,
                     "Save slot not found: %s", slot_name);
        return FALSE;
    }

    file = g_file_new_for_path (path);

    if (!g_file_delete (file, NULL, error))
    {
        return FALSE;
    }

    lrg_log_info ("Deleted save: %s", slot_name);

    return TRUE;
}

/*
 * lrg_save_manager_slot_exists:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 *
 * Checks if a save slot exists.
 *
 * Returns: %TRUE if the slot exists
 */
gboolean
lrg_save_manager_slot_exists (LrgSaveManager *self,
                              const gchar    *slot_name)
{
    g_autofree gchar *path = NULL;

    g_return_val_if_fail (LRG_IS_SAVE_MANAGER (self), FALSE);
    g_return_val_if_fail (slot_name != NULL, FALSE);

    path = get_slot_path (self, slot_name);

    return g_file_test (path, G_FILE_TEST_EXISTS);
}
