/* lrg-template-loading-state.h - Loading screen state for game templates
 *
 * Copyright 2025 Zach Podbielniak
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This file is part of Libregnum.
 */

#ifndef LRG_TEMPLATE_LOADING_STATE_H
#define LRG_TEMPLATE_LOADING_STATE_H

#include <glib-object.h>
#include <graylib.h>
#include "../../lrg-version.h"
#include "../../gamestate/lrg-game-state.h"

G_BEGIN_DECLS

#define LRG_TYPE_TEMPLATE_LOADING_STATE (lrg_template_loading_state_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgTemplateLoadingState, lrg_template_loading_state,
                          LRG, TEMPLATE_LOADING_STATE, LrgGameState)

/**
 * LrgTemplateLoadingStateClass:
 * @parent_class: the parent class
 * @on_complete: Called when loading completes
 * @on_failed: Called when loading fails
 *
 * The class structure for #LrgTemplateLoadingState.
 */
struct _LrgTemplateLoadingStateClass
{
    LrgGameStateClass parent_class;

    /*< public >*/

    /**
     * LrgTemplateLoadingStateClass::on_complete:
     * @self: an #LrgTemplateLoadingState
     *
     * Called when all loading tasks complete successfully.
     * Default implementation emits the ::complete signal.
     */
    void (*on_complete) (LrgTemplateLoadingState *self);

    /**
     * LrgTemplateLoadingStateClass::on_failed:
     * @self: an #LrgTemplateLoadingState
     * @error: the error that occurred
     *
     * Called when a loading task fails.
     * Default implementation emits the ::failed signal.
     */
    void (*on_failed)   (LrgTemplateLoadingState *self,
                         GError                  *error);

    /*< private >*/
    gpointer _reserved[8];
};

/**
 * LrgLoadingTask:
 *
 * A callback function type for loading tasks.
 *
 * Returns: %TRUE on success, %FALSE on failure (set error)
 */
typedef gboolean (*LrgLoadingTask) (gpointer   user_data,
                                    GError   **error);

/* ==========================================================================
 * Construction
 * ========================================================================== */

/**
 * lrg_template_loading_state_new:
 *
 * Creates a new loading state.
 *
 * Returns: (transfer full): A new #LrgTemplateLoadingState
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTemplateLoadingState *
lrg_template_loading_state_new (void);

/* ==========================================================================
 * Task Management
 * ========================================================================== */

/**
 * lrg_template_loading_state_add_task:
 * @self: an #LrgTemplateLoadingState
 * @name: display name for the task
 * @task: the loading task callback
 * @user_data: (nullable): data to pass to the callback
 * @destroy: (nullable): destroy function for user_data
 *
 * Adds a loading task. Tasks are executed one per frame.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_add_task (LrgTemplateLoadingState *self,
                                     const gchar             *name,
                                     LrgLoadingTask           task,
                                     gpointer                 user_data,
                                     GDestroyNotify           destroy);

/**
 * lrg_template_loading_state_add_asset:
 * @self: an #LrgTemplateLoadingState
 * @asset_path: path to the asset file
 *
 * Adds an asset to load. This is a convenience method that uses
 * the asset manager to load the file.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_add_asset (LrgTemplateLoadingState *self,
                                      const gchar             *asset_path);

/**
 * lrg_template_loading_state_clear_tasks:
 * @self: an #LrgTemplateLoadingState
 *
 * Clears all pending loading tasks.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_clear_tasks (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_get_task_count:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the total number of tasks.
 *
 * Returns: The total task count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_template_loading_state_get_task_count (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_get_completed_count:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the number of completed tasks.
 *
 * Returns: The completed task count
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint
lrg_template_loading_state_get_completed_count (LrgTemplateLoadingState *self);

/* ==========================================================================
 * Progress
 * ========================================================================== */

/**
 * lrg_template_loading_state_get_progress:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the current loading progress (0.0 to 1.0).
 *
 * Returns: The progress fraction
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_loading_state_get_progress (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_get_current_task_name:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the name of the currently loading task.
 *
 * Returns: (transfer none) (nullable): The current task name
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_loading_state_get_current_task_name (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_is_complete:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets whether loading has completed.
 *
 * Returns: %TRUE if all tasks are complete
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_loading_state_is_complete (LrgTemplateLoadingState *self);

/* ==========================================================================
 * Minimum Display Time
 * ========================================================================== */

/**
 * lrg_template_loading_state_get_minimum_display_time:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the minimum time the loading screen is displayed.
 *
 * Returns: The minimum display time in seconds
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gdouble
lrg_template_loading_state_get_minimum_display_time (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_set_minimum_display_time:
 * @self: an #LrgTemplateLoadingState
 * @time: minimum display time in seconds
 *
 * Sets the minimum time the loading screen is displayed. Even if loading
 * completes faster, the screen stays visible for this duration.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_set_minimum_display_time (LrgTemplateLoadingState *self,
                                                     gdouble                  time);

/* ==========================================================================
 * Appearance
 * ========================================================================== */

/**
 * lrg_template_loading_state_get_background_color:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the background color.
 *
 * Returns: (transfer none): The background color
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const GrlColor *
lrg_template_loading_state_get_background_color (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_set_background_color:
 * @self: an #LrgTemplateLoadingState
 * @color: the background color
 *
 * Sets the background color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_set_background_color (LrgTemplateLoadingState *self,
                                                 const GrlColor          *color);

/**
 * lrg_template_loading_state_get_status_text:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets the status text displayed above the progress bar.
 *
 * Returns: (transfer none) (nullable): The status text
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar *
lrg_template_loading_state_get_status_text (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_set_status_text:
 * @self: an #LrgTemplateLoadingState
 * @text: (nullable): the status text
 *
 * Sets the status text displayed above the progress bar.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_set_status_text (LrgTemplateLoadingState *self,
                                            const gchar             *text);

/**
 * lrg_template_loading_state_get_show_progress_bar:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets whether the progress bar is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_loading_state_get_show_progress_bar (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_set_show_progress_bar:
 * @self: an #LrgTemplateLoadingState
 * @show: whether to show progress bar
 *
 * Sets whether the progress bar is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_set_show_progress_bar (LrgTemplateLoadingState *self,
                                                  gboolean                 show);

/**
 * lrg_template_loading_state_get_show_percentage:
 * @self: an #LrgTemplateLoadingState
 *
 * Gets whether the percentage text is shown.
 *
 * Returns: %TRUE if shown
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_template_loading_state_get_show_percentage (LrgTemplateLoadingState *self);

/**
 * lrg_template_loading_state_set_show_percentage:
 * @self: an #LrgTemplateLoadingState
 * @show: whether to show percentage
 *
 * Sets whether the percentage text is shown.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_template_loading_state_set_show_percentage (LrgTemplateLoadingState *self,
                                                gboolean                 show);

G_END_DECLS

#endif /* LRG_TEMPLATE_LOADING_STATE_H */
