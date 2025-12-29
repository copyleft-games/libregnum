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
#include "../lrg-log.h"
#include <gio/gio.h>

struct _LrgSaveManager
{
    GObject parent_instance;

    gchar *save_directory;
    guint  save_version;

    /* Registered saveables: save_id -> LrgSaveable* */
    GHashTable *saveables;
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
    return g_build_filename (self->save_directory,
                             g_strdup_printf ("%s.yaml", slot_name),
                             NULL);
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

static void
lrg_save_manager_finalize (GObject *object)
{
    LrgSaveManager *self = LRG_SAVE_MANAGER (object);

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
                           0, G_MAXUINT, 1,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
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

    /**
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

    /**
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

    /**
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

/**
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

/**
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

/**
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

/**
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

/**
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

/**
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

    if (self->save_version == version)
        return;

    self->save_version = version;

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SAVE_VERSION]);
}

/* ==========================================================================
 * Saveable Registration
 * ========================================================================== */

/**
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

/**
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

/**
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

/**
 * lrg_save_manager_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
 *
 * Saves the game state to the specified slot.
 *
 * Returns: %TRUE on success
 */
gboolean
lrg_save_manager_save (LrgSaveManager  *self,
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

    ensure_directory_exists (self->save_directory);

    g_signal_emit (self, signals[SIGNAL_SAVE_STARTED], 0, slot_name);

    lrg_log_info ("Saving to slot: %s", slot_name);

    context = lrg_save_context_new_for_save ();
    lrg_save_context_set_version (context, self->save_version);

    /* Write metadata section */
    lrg_save_context_begin_section (context, "metadata");
    lrg_save_context_write_string (context, "slot_name", slot_name);
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
            success = FALSE;
            break;
        }

        lrg_save_context_end_section (context);
    }

    if (success)
    {
        path = get_slot_path (self, slot_name);
        success = lrg_save_context_to_file (context, path, error);
    }

    g_signal_emit (self, signals[SIGNAL_SAVE_COMPLETED], 0, slot_name, success);

    if (success)
    {
        lrg_log_info ("Saved successfully to: %s", slot_name);
    }

    return success;
}

/**
 * lrg_save_manager_load:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
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

/**
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
        save = lrg_save_game_new_from_file (path, NULL);

        if (save != NULL)
        {
            saves = g_list_prepend (saves, save);
        }
    }

    return g_list_reverse (saves);
}

/**
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

    return lrg_save_game_new_from_file (path, NULL);
}

/**
 * lrg_save_manager_delete_save:
 * @self: a #LrgSaveManager
 * @slot_name: the slot identifier
 * @error: (optional): return location for a #GError
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

/**
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
