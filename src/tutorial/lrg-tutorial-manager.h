/* lrg-tutorial-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial manager for managing multiple tutorials.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-tutorial.h"

G_BEGIN_DECLS

#define LRG_TYPE_TUTORIAL_MANAGER (lrg_tutorial_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTutorialManager, lrg_tutorial_manager, LRG, TUTORIAL_MANAGER, GObject)

/**
 * lrg_tutorial_manager_new:
 *
 * Creates a new tutorial manager.
 *
 * Returns: (transfer full): A new #LrgTutorialManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialManager * lrg_tutorial_manager_new                  (void);

/* Tutorial registration */

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_register               (LrgTutorialManager *self,
                                                                 LrgTutorial        *tutorial);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_unregister             (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

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
LRG_AVAILABLE_IN_ALL
LrgTutorial *       lrg_tutorial_manager_get_tutorial           (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

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
LRG_AVAILABLE_IN_ALL
GList *             lrg_tutorial_manager_get_tutorials          (LrgTutorialManager *self);

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
LRG_AVAILABLE_IN_ALL
LrgTutorial *       lrg_tutorial_manager_load_from_file         (LrgTutorialManager  *self,
                                                                 const gchar         *path,
                                                                 GError             **error);

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
LRG_AVAILABLE_IN_ALL
guint               lrg_tutorial_manager_load_from_directory    (LrgTutorialManager  *self,
                                                                 const gchar         *directory,
                                                                 GError             **error);

/* Active tutorial management */

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
LRG_AVAILABLE_IN_ALL
LrgTutorial *       lrg_tutorial_manager_get_active_tutorial    (LrgTutorialManager *self);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_start_tutorial         (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

/**
 * lrg_tutorial_manager_stop_active:
 * @self: A #LrgTutorialManager
 *
 * Stops the currently active tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_stop_active            (LrgTutorialManager *self);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_skip_active            (LrgTutorialManager *self);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_advance_active         (LrgTutorialManager *self);

/* Completion status */

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_is_completed           (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

/**
 * lrg_tutorial_manager_mark_completed:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to mark
 *
 * Marks a tutorial as completed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_mark_completed         (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

/**
 * lrg_tutorial_manager_clear_completion:
 * @self: A #LrgTutorialManager
 * @tutorial_id: ID of the tutorial to clear
 *
 * Clears completion status for a tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_clear_completion       (LrgTutorialManager *self,
                                                                 const gchar        *tutorial_id);

/**
 * lrg_tutorial_manager_clear_all_completions:
 * @self: A #LrgTutorialManager
 *
 * Clears all completion statuses.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_clear_all_completions  (LrgTutorialManager *self);

/* Update */

/**
 * lrg_tutorial_manager_update:
 * @self: A #LrgTutorialManager
 * @delta_time: Time since last update in seconds
 *
 * Updates the tutorial manager and active tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_update                 (LrgTutorialManager *self,
                                                                 gfloat              delta_time);

/* Persistence */

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_save_progress          (LrgTutorialManager  *self,
                                                                 const gchar         *path,
                                                                 GError             **error);

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
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_manager_load_progress          (LrgTutorialManager  *self,
                                                                 const gchar         *path,
                                                                 GError             **error);

/* Condition callback */

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
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_manager_set_condition_callback (LrgTutorialManager       *self,
                                                                 LrgTutorialConditionFunc  callback,
                                                                 gpointer                  user_data,
                                                                 GDestroyNotify            destroy);

G_END_DECLS
