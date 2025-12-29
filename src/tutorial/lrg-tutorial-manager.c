/* lrg-tutorial-manager.c
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial manager for managing multiple tutorials.
 */

#include "lrg-tutorial-manager.h"

#include <gio/gio.h>

struct _LrgTutorialManager
{
    GObject parent_instance;

    GHashTable  *tutorials;         /* id -> LrgTutorial* */
    GHashTable  *completed;         /* id -> gboolean (TRUE if completed) */
    LrgTutorial *active_tutorial;

    /* Global condition callback */
    LrgTutorialConditionFunc condition_callback;
    gpointer                 condition_user_data;
    GDestroyNotify           condition_destroy;
};

G_DEFINE_FINAL_TYPE (LrgTutorialManager, lrg_tutorial_manager, G_TYPE_OBJECT)

enum
{
    PROP_0,
    PROP_ACTIVE_TUTORIAL,
    PROP_TUTORIAL_COUNT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

enum
{
    SIGNAL_TUTORIAL_STARTED,
    SIGNAL_TUTORIAL_COMPLETED,
    SIGNAL_TUTORIAL_SKIPPED,
    SIGNAL_STEP_CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
on_tutorial_started (LrgTutorial        *tutorial,
                     LrgTutorialManager *self)
{
    g_signal_emit (self, signals[SIGNAL_TUTORIAL_STARTED], 0, tutorial);
}

static void
on_tutorial_completed (LrgTutorial        *tutorial,
                       LrgTutorialManager *self)
{
    const gchar *id = lrg_tutorial_get_id (tutorial);

    /* Mark as completed */
    if (id != NULL)
    {
        g_hash_table_insert (self->completed, g_strdup (id), GINT_TO_POINTER (TRUE));
    }

    g_signal_emit (self, signals[SIGNAL_TUTORIAL_COMPLETED], 0, tutorial);

    /* Clear active tutorial */
    if (self->active_tutorial == tutorial)
    {
        self->active_tutorial = NULL;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TUTORIAL]);
    }
}

static void
on_tutorial_skipped (LrgTutorial        *tutorial,
                     LrgTutorialManager *self)
{
    const gchar *id = lrg_tutorial_get_id (tutorial);

    /* Mark as completed (skipping counts as completion) */
    if (id != NULL)
    {
        g_hash_table_insert (self->completed, g_strdup (id), GINT_TO_POINTER (TRUE));
    }

    g_signal_emit (self, signals[SIGNAL_TUTORIAL_SKIPPED], 0, tutorial);

    /* Clear active tutorial */
    if (self->active_tutorial == tutorial)
    {
        self->active_tutorial = NULL;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TUTORIAL]);
    }
}

static void
on_tutorial_step_changed (LrgTutorial        *tutorial,
                          guint               step_index,
                          LrgTutorialStep    *step,
                          LrgTutorialManager *self)
{
    g_signal_emit (self, signals[SIGNAL_STEP_CHANGED], 0, tutorial, step_index, step);
}

static void
lrg_tutorial_manager_finalize (GObject *object)
{
    LrgTutorialManager *self = LRG_TUTORIAL_MANAGER (object);

    g_clear_pointer (&self->tutorials, g_hash_table_unref);
    g_clear_pointer (&self->completed, g_hash_table_unref);

    if (self->condition_destroy != NULL && self->condition_user_data != NULL)
    {
        self->condition_destroy (self->condition_user_data);
    }

    G_OBJECT_CLASS (lrg_tutorial_manager_parent_class)->finalize (object);
}

static void
lrg_tutorial_manager_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
    LrgTutorialManager *self = LRG_TUTORIAL_MANAGER (object);

    switch (prop_id)
    {
    case PROP_ACTIVE_TUTORIAL:
        g_value_set_object (value, self->active_tutorial);
        break;
    case PROP_TUTORIAL_COUNT:
        g_value_set_uint (value, g_hash_table_size (self->tutorials));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
lrg_tutorial_manager_class_init (LrgTutorialManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = lrg_tutorial_manager_finalize;
    object_class->get_property = lrg_tutorial_manager_get_property;

    /**
     * LrgTutorialManager:active-tutorial:
     *
     * The currently active tutorial.
     *
     * Since: 1.0
     */
    properties[PROP_ACTIVE_TUTORIAL] =
        g_param_spec_object ("active-tutorial", NULL, NULL,
                             LRG_TYPE_TUTORIAL,
                             G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    /**
     * LrgTutorialManager:tutorial-count:
     *
     * Number of registered tutorials.
     *
     * Since: 1.0
     */
    properties[PROP_TUTORIAL_COUNT] =
        g_param_spec_uint ("tutorial-count", NULL, NULL,
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    /**
     * LrgTutorialManager::tutorial-started:
     * @self: The manager
     * @tutorial: The tutorial that started
     *
     * Emitted when a tutorial starts.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TUTORIAL_STARTED] =
        g_signal_new ("tutorial-started",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TUTORIAL);

    /**
     * LrgTutorialManager::tutorial-completed:
     * @self: The manager
     * @tutorial: The tutorial that completed
     *
     * Emitted when a tutorial completes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TUTORIAL_COMPLETED] =
        g_signal_new ("tutorial-completed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TUTORIAL);

    /**
     * LrgTutorialManager::tutorial-skipped:
     * @self: The manager
     * @tutorial: The tutorial that was skipped
     *
     * Emitted when a tutorial is skipped.
     *
     * Since: 1.0
     */
    signals[SIGNAL_TUTORIAL_SKIPPED] =
        g_signal_new ("tutorial-skipped",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1,
                      LRG_TYPE_TUTORIAL);

    /**
     * LrgTutorialManager::step-changed:
     * @self: The manager
     * @tutorial: The active tutorial
     * @step_index: The new step index
     * @step: The new step
     *
     * Emitted when the active tutorial's step changes.
     *
     * Since: 1.0
     */
    signals[SIGNAL_STEP_CHANGED] =
        g_signal_new ("step-changed",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 3,
                      LRG_TYPE_TUTORIAL,
                      G_TYPE_UINT,
                      LRG_TYPE_TUTORIAL_STEP);
}

static void
lrg_tutorial_manager_init (LrgTutorialManager *self)
{
    self->tutorials = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, g_object_unref);
    self->completed = g_hash_table_new_full (g_str_hash, g_str_equal,
                                             g_free, NULL);
    self->active_tutorial = NULL;
}

/**
 * lrg_tutorial_manager_new:
 *
 * Creates a new tutorial manager.
 *
 * Returns: (transfer full): A new #LrgTutorialManager
 *
 * Since: 1.0
 */
LrgTutorialManager *
lrg_tutorial_manager_new (void)
{
    return g_object_new (LRG_TYPE_TUTORIAL_MANAGER, NULL);
}

/**
 * lrg_tutorial_manager_register:
 * @self: A #LrgTutorialManager
 * @tutorial: (transfer none): The tutorial to register
 *
 * Registers a tutorial with the manager.
 *
 * Returns: %TRUE if registered successfully
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_register (LrgTutorialManager *self,
                               LrgTutorial        *tutorial)
{
    const gchar *id;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (LRG_IS_TUTORIAL (tutorial), FALSE);

    id = lrg_tutorial_get_id (tutorial);
    if (id == NULL)
        return FALSE;

    if (g_hash_table_contains (self->tutorials, id))
        return FALSE;

    g_hash_table_insert (self->tutorials, g_strdup (id), g_object_ref (tutorial));

    /* Connect to tutorial signals */
    g_signal_connect (tutorial, "started",
                      G_CALLBACK (on_tutorial_started), self);
    g_signal_connect (tutorial, "completed",
                      G_CALLBACK (on_tutorial_completed), self);
    g_signal_connect (tutorial, "skipped",
                      G_CALLBACK (on_tutorial_skipped), self);
    g_signal_connect (tutorial, "step-changed",
                      G_CALLBACK (on_tutorial_step_changed), self);

    /* Apply global condition callback if set */
    if (self->condition_callback != NULL)
    {
        lrg_tutorial_set_condition_callback (tutorial,
                                             self->condition_callback,
                                             self->condition_user_data,
                                             NULL);
    }

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TUTORIAL_COUNT]);

    return TRUE;
}

/**
 * lrg_tutorial_manager_unregister:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to unregister
 *
 * Unregisters a tutorial from the manager.
 *
 * Returns: %TRUE if unregistered successfully
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_unregister (LrgTutorialManager *self,
                                 const gchar        *tutorial_id)
{
    LrgTutorial *tutorial;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (tutorial_id != NULL, FALSE);

    tutorial = g_hash_table_lookup (self->tutorials, tutorial_id);
    if (tutorial == NULL)
        return FALSE;

    /* Stop if this is the active tutorial */
    if (self->active_tutorial == tutorial)
    {
        lrg_tutorial_reset (tutorial);
        self->active_tutorial = NULL;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TUTORIAL]);
    }

    /* Disconnect signals */
    g_signal_handlers_disconnect_by_data (tutorial, self);

    g_hash_table_remove (self->tutorials, tutorial_id);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TUTORIAL_COUNT]);

    return TRUE;
}

/**
 * lrg_tutorial_manager_get_tutorial:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to get
 *
 * Gets a registered tutorial by ID.
 *
 * Returns: (transfer none) (nullable): The tutorial, or %NULL if not found
 *
 * Since: 1.0
 */
LrgTutorial *
lrg_tutorial_manager_get_tutorial (LrgTutorialManager *self,
                                   const gchar        *tutorial_id)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), NULL);
    g_return_val_if_fail (tutorial_id != NULL, NULL);

    return g_hash_table_lookup (self->tutorials, tutorial_id);
}

/**
 * lrg_tutorial_manager_get_tutorials:
 * @self: A #LrgTutorialManager
 *
 * Gets all registered tutorials.
 *
 * Returns: (transfer container) (element-type LrgTutorial): List of tutorials
 *
 * Since: 1.0
 */
GList *
lrg_tutorial_manager_get_tutorials (LrgTutorialManager *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), NULL);

    return g_hash_table_get_values (self->tutorials);
}

/**
 * lrg_tutorial_manager_load_from_file:
 * @self: A #LrgTutorialManager
 * @path: Path to the tutorial definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Loads and registers a tutorial from a file.
 *
 * Returns: (transfer none) (nullable): The loaded tutorial, or %NULL on error
 *
 * Since: 1.0
 */
LrgTutorial *
lrg_tutorial_manager_load_from_file (LrgTutorialManager  *self,
                                     const gchar         *path,
                                     GError             **error)
{
    g_autoptr(LrgTutorial) tutorial = NULL;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), NULL);
    g_return_val_if_fail (path != NULL, NULL);

    tutorial = lrg_tutorial_new_from_file (path, error);
    if (tutorial == NULL)
        return NULL;

    if (!lrg_tutorial_manager_register (self, tutorial))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                     "Tutorial with ID '%s' already registered",
                     lrg_tutorial_get_id (tutorial));
        return NULL;
    }

    return lrg_tutorial_manager_get_tutorial (self, lrg_tutorial_get_id (tutorial));
}

/**
 * lrg_tutorial_manager_load_from_directory:
 * @self: A #LrgTutorialManager
 * @directory: Path to directory containing tutorial files
 * @error: (nullable): Return location for error
 *
 * Loads all tutorial files from a directory.
 *
 * Returns: Number of tutorials loaded
 *
 * Since: 1.0
 */
guint
lrg_tutorial_manager_load_from_directory (LrgTutorialManager  *self,
                                          const gchar         *directory,
                                          GError             **error)
{
    g_autoptr(GDir) dir = NULL;
    const gchar *filename;
    guint count = 0;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), 0);
    g_return_val_if_fail (directory != NULL, 0);

    dir = g_dir_open (directory, 0, error);
    if (dir == NULL)
        return 0;

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        g_autofree gchar *path = NULL;
        g_autoptr(GError) local_error = NULL;

        /* Only load .yaml and .yml files */
        if (!g_str_has_suffix (filename, ".yaml") &&
            !g_str_has_suffix (filename, ".yml"))
            continue;

        path = g_build_filename (directory, filename, NULL);

        if (lrg_tutorial_manager_load_from_file (self, path, &local_error) != NULL)
        {
            count++;
        }
        /* Ignore errors for individual files, continue loading others */
    }

    return count;
}

/**
 * lrg_tutorial_manager_get_active_tutorial:
 * @self: A #LrgTutorialManager
 *
 * Gets the currently active tutorial.
 *
 * Returns: (transfer none) (nullable): The active tutorial, or %NULL
 *
 * Since: 1.0
 */
LrgTutorial *
lrg_tutorial_manager_get_active_tutorial (LrgTutorialManager *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), NULL);
    return self->active_tutorial;
}

/**
 * lrg_tutorial_manager_start_tutorial:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to start
 *
 * Starts a tutorial by ID.
 *
 * Returns: %TRUE if the tutorial was started
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_start_tutorial (LrgTutorialManager *self,
                                     const gchar        *tutorial_id)
{
    LrgTutorial *tutorial;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (tutorial_id != NULL, FALSE);

    /* Stop any active tutorial first */
    if (self->active_tutorial != NULL)
    {
        lrg_tutorial_manager_stop_active (self);
    }

    tutorial = g_hash_table_lookup (self->tutorials, tutorial_id);
    if (tutorial == NULL)
        return FALSE;

    if (!lrg_tutorial_start (tutorial))
        return FALSE;

    self->active_tutorial = tutorial;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TUTORIAL]);

    return TRUE;
}

/**
 * lrg_tutorial_manager_stop_active:
 * @self: A #LrgTutorialManager
 *
 * Stops the currently active tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_stop_active (LrgTutorialManager *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));

    if (self->active_tutorial == NULL)
        return;

    lrg_tutorial_reset (self->active_tutorial);
    self->active_tutorial = NULL;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE_TUTORIAL]);
}

/**
 * lrg_tutorial_manager_skip_active:
 * @self: A #LrgTutorialManager
 *
 * Skips the currently active tutorial.
 *
 * Returns: %TRUE if the tutorial was skipped
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_skip_active (LrgTutorialManager *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);

    if (self->active_tutorial == NULL)
        return FALSE;

    return lrg_tutorial_skip (self->active_tutorial);
}

/**
 * lrg_tutorial_manager_advance_active:
 * @self: A #LrgTutorialManager
 *
 * Advances the active tutorial to the next step.
 *
 * Returns: %TRUE if advanced successfully
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_advance_active (LrgTutorialManager *self)
{
    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);

    if (self->active_tutorial == NULL)
        return FALSE;

    return lrg_tutorial_advance (self->active_tutorial);
}

/**
 * lrg_tutorial_manager_is_completed:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to check
 *
 * Checks if a tutorial has been completed.
 *
 * Returns: %TRUE if completed
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_is_completed (LrgTutorialManager *self,
                                   const gchar        *tutorial_id)
{
    gpointer value;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (tutorial_id != NULL, FALSE);

    if (g_hash_table_lookup_extended (self->completed, tutorial_id, NULL, &value))
    {
        return GPOINTER_TO_INT (value);
    }

    return FALSE;
}

/**
 * lrg_tutorial_manager_mark_completed:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to mark
 *
 * Marks a tutorial as completed.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_mark_completed (LrgTutorialManager *self,
                                     const gchar        *tutorial_id)
{
    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));
    g_return_if_fail (tutorial_id != NULL);

    g_hash_table_insert (self->completed, g_strdup (tutorial_id),
                         GINT_TO_POINTER (TRUE));
}

/**
 * lrg_tutorial_manager_clear_completion:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to clear
 *
 * Clears completion status for a tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_clear_completion (LrgTutorialManager *self,
                                       const gchar        *tutorial_id)
{
    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));
    g_return_if_fail (tutorial_id != NULL);

    g_hash_table_remove (self->completed, tutorial_id);
}

/**
 * lrg_tutorial_manager_clear_all_completions:
 * @self: A #LrgTutorialManager
 *
 * Clears all completion statuses.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_clear_all_completions (LrgTutorialManager *self)
{
    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));

    g_hash_table_remove_all (self->completed);
}

/**
 * lrg_tutorial_manager_update:
 * @self: A #LrgTutorialManager
 * @delta_time: Time since last update in seconds
 *
 * Updates the tutorial manager and active tutorial.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_update (LrgTutorialManager *self,
                             gfloat              delta_time)
{
    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));

    if (self->active_tutorial != NULL)
    {
        lrg_tutorial_update (self->active_tutorial, delta_time);
    }
}

/**
 * lrg_tutorial_manager_save_progress:
 * @self: A #LrgTutorialManager
 * @path: Path to save progress to
 * @error: (nullable): Return location for error
 *
 * Saves tutorial completion progress to a file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_save_progress (LrgTutorialManager  *self,
                                    const gchar         *path,
                                    GError             **error)
{
    g_autoptr(GString) content = NULL;
    GHashTableIter iter;
    gpointer key, value;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    /*
     * Simple YAML format:
     * completed:
     *   - tutorial_id_1
     *   - tutorial_id_2
     */
    content = g_string_new ("completed:\n");

    g_hash_table_iter_init (&iter, self->completed);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        if (GPOINTER_TO_INT (value))
        {
            g_string_append_printf (content, "  - \"%s\"\n", (const gchar *)key);
        }
    }

    return g_file_set_contents (path, content->str, (gssize)content->len, error);
}

/**
 * lrg_tutorial_manager_load_progress:
 * @self: A #LrgTutorialManager
 * @path: Path to load progress from
 * @error: (nullable): Return location for error
 *
 * Loads tutorial completion progress from a file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
gboolean
lrg_tutorial_manager_load_progress (LrgTutorialManager  *self,
                                    const gchar         *path,
                                    GError             **error)
{
    g_autofree gchar *content = NULL;
    gchar **lines;
    guint i;

    g_return_val_if_fail (LRG_IS_TUTORIAL_MANAGER (self), FALSE);
    g_return_val_if_fail (path != NULL, FALSE);

    if (!g_file_get_contents (path, &content, NULL, error))
        return FALSE;

    /* Clear existing completion data */
    g_hash_table_remove_all (self->completed);

    /*
     * Simple parser for our YAML format:
     * completed:
     *   - tutorial_id_1
     *   - tutorial_id_2
     */
    lines = g_strsplit (content, "\n", -1);

    for (i = 0; lines[i] != NULL; i++)
    {
        gchar *line = g_strstrip (lines[i]);
        gchar *id;

        if (g_str_has_prefix (line, "- "))
        {
            id = g_strstrip (line + 2);

            /* Remove quotes if present */
            if (id[0] == '"' && id[strlen (id) - 1] == '"')
            {
                id[strlen (id) - 1] = '\0';
                id++;
            }

            if (strlen (id) > 0)
            {
                g_hash_table_insert (self->completed, g_strdup (id),
                                     GINT_TO_POINTER (TRUE));
            }
        }
    }

    g_strfreev (lines);

    return TRUE;
}

/**
 * lrg_tutorial_manager_set_condition_callback:
 * @self: A #LrgTutorialManager
 * @callback: (scope notified) (nullable): The callback function
 * @user_data: User data to pass to the callback
 * @destroy: (nullable): Destroy notify for user_data
 *
 * Sets a global condition callback that applies to all tutorials.
 *
 * Since: 1.0
 */
void
lrg_tutorial_manager_set_condition_callback (LrgTutorialManager       *self,
                                             LrgTutorialConditionFunc  callback,
                                             gpointer                  user_data,
                                             GDestroyNotify            destroy)
{
    GHashTableIter iter;
    gpointer value;

    g_return_if_fail (LRG_IS_TUTORIAL_MANAGER (self));

    /* Clean up old callback */
    if (self->condition_destroy != NULL && self->condition_user_data != NULL)
    {
        self->condition_destroy (self->condition_user_data);
    }

    self->condition_callback = callback;
    self->condition_user_data = user_data;
    self->condition_destroy = destroy;

    /* Apply to all registered tutorials */
    g_hash_table_iter_init (&iter, self->tutorials);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        LrgTutorial *tutorial = LRG_TUTORIAL (value);
        lrg_tutorial_set_condition_callback (tutorial, callback, user_data, NULL);
    }
}
