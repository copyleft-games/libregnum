/* lrg-tutorial.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Tutorial sequence definition.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-enums.h"
#include "lrg-tutorial-step.h"

G_BEGIN_DECLS

#define LRG_TYPE_TUTORIAL (lrg_tutorial_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTutorial, lrg_tutorial, LRG, TUTORIAL, GObject)

/**
 * lrg_tutorial_new:
 * @id: Unique identifier for the tutorial
 * @name: Display name of the tutorial
 *
 * Creates a new tutorial.
 *
 * Returns: (transfer full): A new #LrgTutorial
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorial *       lrg_tutorial_new                        (const gchar *id,
                                                             const gchar *name);

/**
 * lrg_tutorial_new_from_file:
 * @path: Path to the tutorial definition file (YAML)
 * @error: (nullable): Return location for error
 *
 * Creates a tutorial by loading a definition file.
 *
 * Returns: (transfer full) (nullable): A new #LrgTutorial or %NULL on error
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorial *       lrg_tutorial_new_from_file              (const gchar  *path,
                                                             GError      **error);

/* Properties */

/**
 * lrg_tutorial_get_id:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial ID.
 *
 * Returns: (transfer none): The tutorial ID
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_get_id                     (LrgTutorial *self);

/**
 * lrg_tutorial_get_name:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial display name.
 *
 * Returns: (transfer none): The tutorial name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_get_name                   (LrgTutorial *self);

/**
 * lrg_tutorial_get_description:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial description.
 *
 * Returns: (transfer none) (nullable): The description
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *       lrg_tutorial_get_description            (LrgTutorial *self);

/**
 * lrg_tutorial_set_description:
 * @self: A #LrgTutorial
 * @description: The description
 *
 * Sets the tutorial description.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_set_description            (LrgTutorial *self,
                                                             const gchar *description);

/**
 * lrg_tutorial_get_state:
 * @self: A #LrgTutorial
 *
 * Gets the current state of the tutorial.
 *
 * Returns: The tutorial state
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialState    lrg_tutorial_get_state                  (LrgTutorial *self);

/**
 * lrg_tutorial_is_repeatable:
 * @self: A #LrgTutorial
 *
 * Gets whether the tutorial can be replayed.
 *
 * Returns: %TRUE if repeatable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_is_repeatable              (LrgTutorial *self);

/**
 * lrg_tutorial_set_repeatable:
 * @self: A #LrgTutorial
 * @repeatable: Whether the tutorial is repeatable
 *
 * Sets whether the tutorial can be replayed.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_set_repeatable             (LrgTutorial *self,
                                                             gboolean     repeatable);

/**
 * lrg_tutorial_is_skippable:
 * @self: A #LrgTutorial
 *
 * Gets whether the entire tutorial can be skipped.
 *
 * Returns: %TRUE if skippable
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_is_skippable               (LrgTutorial *self);

/**
 * lrg_tutorial_set_skippable:
 * @self: A #LrgTutorial
 * @skippable: Whether the tutorial is skippable
 *
 * Sets whether the entire tutorial can be skipped.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_set_skippable              (LrgTutorial *self,
                                                             gboolean     skippable);

/* Step management */

/**
 * lrg_tutorial_get_step_count:
 * @self: A #LrgTutorial
 *
 * Gets the total number of steps.
 *
 * Returns: The step count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tutorial_get_step_count             (LrgTutorial *self);

/**
 * lrg_tutorial_get_step:
 * @self: A #LrgTutorial
 * @index: Step index (0-based)
 *
 * Gets a step by index.
 *
 * Returns: (transfer none) (nullable): The step, or %NULL if out of bounds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_get_step                   (LrgTutorial *self,
                                                             guint        index);

/**
 * lrg_tutorial_get_step_by_id:
 * @self: A #LrgTutorial
 * @id: Step ID
 *
 * Gets a step by ID.
 *
 * Returns: (transfer none) (nullable): The step, or %NULL if not found
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_get_step_by_id             (LrgTutorial *self,
                                                             const gchar *id);

/**
 * lrg_tutorial_add_step:
 * @self: A #LrgTutorial
 * @step: The step to add (copied)
 *
 * Adds a step to the end of the tutorial.
 *
 * Returns: The index of the added step
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tutorial_add_step                   (LrgTutorial           *self,
                                                             const LrgTutorialStep *step);

/**
 * lrg_tutorial_insert_step:
 * @self: A #LrgTutorial
 * @index: Position to insert at
 * @step: The step to insert (copied)
 *
 * Inserts a step at the specified position.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_insert_step                (LrgTutorial           *self,
                                                             guint                  index,
                                                             const LrgTutorialStep *step);

/**
 * lrg_tutorial_remove_step:
 * @self: A #LrgTutorial
 * @index: Index of step to remove
 *
 * Removes a step by index.
 *
 * Returns: %TRUE if the step was removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_remove_step                (LrgTutorial *self,
                                                             guint        index);

/**
 * lrg_tutorial_clear_steps:
 * @self: A #LrgTutorial
 *
 * Removes all steps from the tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_clear_steps                (LrgTutorial *self);

/* Runtime state */

/**
 * lrg_tutorial_get_current_step_index:
 * @self: A #LrgTutorial
 *
 * Gets the current step index.
 *
 * Returns: The current step index, or G_MAXUINT if not active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tutorial_get_current_step_index     (LrgTutorial *self);

/**
 * lrg_tutorial_get_current_step:
 * @self: A #LrgTutorial
 *
 * Gets the current step.
 *
 * Returns: (transfer none) (nullable): The current step, or %NULL if not active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTutorialStep *   lrg_tutorial_get_current_step           (LrgTutorial *self);

/**
 * lrg_tutorial_get_progress:
 * @self: A #LrgTutorial
 *
 * Gets the tutorial progress as a fraction.
 *
 * Returns: Progress from 0.0 to 1.0
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tutorial_get_progress               (LrgTutorial *self);

/* Control */

/**
 * lrg_tutorial_start:
 * @self: A #LrgTutorial
 *
 * Starts the tutorial from the beginning.
 *
 * Returns: %TRUE if the tutorial was started
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_start                      (LrgTutorial *self);

/**
 * lrg_tutorial_pause:
 * @self: A #LrgTutorial
 *
 * Pauses the tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_pause                      (LrgTutorial *self);

/**
 * lrg_tutorial_resume:
 * @self: A #LrgTutorial
 *
 * Resumes a paused tutorial.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_resume                     (LrgTutorial *self);

/**
 * lrg_tutorial_skip:
 * @self: A #LrgTutorial
 *
 * Skips the entire tutorial.
 *
 * Returns: %TRUE if the tutorial was skipped
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_skip                       (LrgTutorial *self);

/**
 * lrg_tutorial_advance:
 * @self: A #LrgTutorial
 *
 * Advances to the next step.
 *
 * Returns: %TRUE if advanced, %FALSE if at end or not active
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_advance                    (LrgTutorial *self);

/**
 * lrg_tutorial_go_to_step:
 * @self: A #LrgTutorial
 * @index: Step index to go to
 *
 * Jumps to a specific step.
 *
 * Returns: %TRUE if successful
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_go_to_step                 (LrgTutorial *self,
                                                             guint        index);

/**
 * lrg_tutorial_reset:
 * @self: A #LrgTutorial
 *
 * Resets the tutorial to inactive state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_reset                      (LrgTutorial *self);

/* Update */

/**
 * lrg_tutorial_update:
 * @self: A #LrgTutorial
 * @delta_time: Time since last update in seconds
 *
 * Updates the tutorial state.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_update                     (LrgTutorial *self,
                                                             gfloat       delta_time);

/* Condition callbacks */

/**
 * LrgTutorialConditionFunc:
 * @condition_id: The condition ID to check
 * @user_data: User data
 *
 * Callback to check if a condition is met.
 *
 * Returns: %TRUE if the condition is met
 *
 * Since: 1.0
 */
typedef gboolean (*LrgTutorialConditionFunc) (const gchar *condition_id,
                                               gpointer     user_data);

/**
 * lrg_tutorial_set_condition_callback:
 * @self: A #LrgTutorial
 * @callback: (scope notified) (nullable): The callback function
 * @user_data: User data to pass to the callback
 * @destroy: (nullable): Destroy notify for user_data
 *
 * Sets the callback for checking conditions.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tutorial_set_condition_callback     (LrgTutorial              *self,
                                                             LrgTutorialConditionFunc  callback,
                                                             gpointer                  user_data,
                                                             GDestroyNotify            destroy);

/* Serialization */

/**
 * lrg_tutorial_save_to_file:
 * @self: A #LrgTutorial
 * @path: Path to save to
 * @error: (nullable): Return location for error
 *
 * Saves the tutorial definition to a YAML file.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tutorial_save_to_file               (LrgTutorial  *self,
                                                             const gchar  *path,
                                                             GError      **error);

G_END_DECLS
