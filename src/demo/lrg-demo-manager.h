/* lrg-demo-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Demo mode manager for controlling trial/demo functionality.
 *
 * The demo manager provides:
 * - Content gating by ID
 * - Optional time limit with warnings
 * - Demo save identification
 * - Purchase URL redirect
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-demo-gatable.h"

G_BEGIN_DECLS

#define LRG_TYPE_DEMO_MANAGER (lrg_demo_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgDemoManager, lrg_demo_manager, LRG, DEMO_MANAGER, GObject)

/* ==========================================================================
 * Constructor
 * ========================================================================== */

/**
 * lrg_demo_manager_new:
 *
 * Creates a new demo manager.
 *
 * Returns: (transfer full): a new #LrgDemoManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDemoManager * lrg_demo_manager_new (void);

/**
 * lrg_demo_manager_get_default:
 *
 * Gets the default demo manager instance.
 *
 * Returns: (transfer none): the default #LrgDemoManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgDemoManager * lrg_demo_manager_get_default (void);

/* ==========================================================================
 * Demo Mode State
 * ========================================================================== */

/**
 * lrg_demo_manager_set_demo_mode:
 * @self: a #LrgDemoManager
 * @is_demo: whether to enable demo mode
 *
 * Sets whether the application is running in demo mode.
 *
 * When demo mode is enabled, content gating and time limits
 * will be enforced.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_set_demo_mode (LrgDemoManager *self,
                                     gboolean        is_demo);

/**
 * lrg_demo_manager_get_demo_mode:
 * @self: a #LrgDemoManager
 *
 * Gets whether the application is running in demo mode.
 *
 * Returns: %TRUE if in demo mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_get_demo_mode (LrgDemoManager *self);

/**
 * lrg_demo_manager_start:
 * @self: a #LrgDemoManager
 *
 * Starts the demo session.
 *
 * This resets the timer and begins tracking demo time.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_start (LrgDemoManager *self);

/**
 * lrg_demo_manager_stop:
 * @self: a #LrgDemoManager
 * @reason: the reason for stopping
 *
 * Stops the demo session.
 *
 * Emits the #LrgDemoManager::demo-ended signal.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_stop (LrgDemoManager   *self,
                            LrgDemoEndReason  reason);

/**
 * lrg_demo_manager_update:
 * @self: a #LrgDemoManager
 * @delta_time: time since last update in seconds
 *
 * Updates the demo manager.
 *
 * Call this each frame to update the time limit tracking.
 * Emits time-warning signals as appropriate.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_update (LrgDemoManager *self,
                              gfloat          delta_time);

/* ==========================================================================
 * Time Limit
 * ========================================================================== */

/**
 * lrg_demo_manager_set_time_limit:
 * @self: a #LrgDemoManager
 * @seconds: time limit in seconds (0 = no limit)
 *
 * Sets the demo time limit in seconds.
 *
 * Set to 0 to disable the time limit.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_set_time_limit (LrgDemoManager *self,
                                      gfloat          seconds);

/**
 * lrg_demo_manager_get_time_limit:
 * @self: a #LrgDemoManager
 *
 * Gets the demo time limit in seconds.
 *
 * Returns: the time limit, or 0 if no limit
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_demo_manager_get_time_limit (LrgDemoManager *self);

/**
 * lrg_demo_manager_get_time_remaining:
 * @self: a #LrgDemoManager
 *
 * Gets the remaining demo time in seconds.
 *
 * Returns: remaining time, or -1 if no limit set
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_demo_manager_get_time_remaining (LrgDemoManager *self);

/**
 * lrg_demo_manager_get_time_elapsed:
 * @self: a #LrgDemoManager
 *
 * Gets the elapsed demo time in seconds.
 *
 * Returns: elapsed time since demo started
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_demo_manager_get_time_elapsed (LrgDemoManager *self);

/**
 * lrg_demo_manager_set_warning_times:
 * @self: a #LrgDemoManager
 * @warning_seconds: (array length=n_warnings): array of warning times
 * @n_warnings: number of warning times
 *
 * Sets the times at which to emit time warnings.
 *
 * For example, to warn at 5 minutes and 1 minute remaining:
 * `lrg_demo_manager_set_warning_times (manager, (gfloat[]){300, 60}, 2);`
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_set_warning_times (LrgDemoManager *self,
                                         const gfloat   *warning_seconds,
                                         guint           n_warnings);

/* ==========================================================================
 * Content Gating
 * ========================================================================== */

/**
 * lrg_demo_manager_gate_content:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to gate
 *
 * Marks content as gated (unavailable in demo mode).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_gate_content (LrgDemoManager *self,
                                    const gchar    *content_id);

/**
 * lrg_demo_manager_ungate_content:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to ungate
 *
 * Removes content from the gated list.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_ungate_content (LrgDemoManager *self,
                                      const gchar    *content_id);

/**
 * lrg_demo_manager_is_content_gated:
 * @self: a #LrgDemoManager
 * @content_id: the content ID to check
 *
 * Checks if content is gated in demo mode.
 *
 * Returns: %TRUE if content is gated
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_is_content_gated (LrgDemoManager *self,
                                            const gchar    *content_id);

/**
 * lrg_demo_manager_check_access:
 * @self: a #LrgDemoManager
 * @gatable: a #LrgDemoGatable object
 * @error: (optional): return location for error
 *
 * Checks if access to gatable content is allowed.
 *
 * Returns %TRUE if not in demo mode or if content is accessible.
 * Returns %FALSE and sets error if content is gated.
 *
 * Emits #LrgDemoManager::content-blocked if access is denied.
 *
 * Returns: %TRUE if access is allowed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_check_access (LrgDemoManager  *self,
                                        LrgDemoGatable  *gatable,
                                        GError         **error);

/**
 * lrg_demo_manager_get_gated_content:
 * @self: a #LrgDemoManager
 *
 * Gets the list of gated content IDs.
 *
 * Returns: (transfer container) (element-type utf8): array of content IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_demo_manager_get_gated_content (LrgDemoManager *self);

/**
 * lrg_demo_manager_clear_gated_content:
 * @self: a #LrgDemoManager
 *
 * Removes all content from the gated list.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_clear_gated_content (LrgDemoManager *self);

/* ==========================================================================
 * Demo Saves
 * ========================================================================== */

/**
 * lrg_demo_manager_mark_save_as_demo:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Marks a save file as being from demo mode.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_mark_save_as_demo (LrgDemoManager *self,
                                         const gchar    *save_id);

/**
 * lrg_demo_manager_is_demo_save:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Checks if a save file is from demo mode.
 *
 * Returns: %TRUE if save is from demo mode
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_is_demo_save (LrgDemoManager *self,
                                        const gchar    *save_id);

/**
 * lrg_demo_manager_get_demo_saves:
 * @self: a #LrgDemoManager
 *
 * Gets all save IDs marked as demo saves.
 *
 * Returns: (transfer container) (element-type utf8): array of save IDs
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray * lrg_demo_manager_get_demo_saves (LrgDemoManager *self);

/**
 * lrg_demo_manager_convert_demo_save:
 * @self: a #LrgDemoManager
 * @save_id: the save identifier
 *
 * Converts a demo save to a full game save.
 *
 * This removes the demo marker from the save.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_convert_demo_save (LrgDemoManager *self,
                                         const gchar    *save_id);

/* ==========================================================================
 * Purchase URL
 * ========================================================================== */

/**
 * lrg_demo_manager_set_purchase_url:
 * @self: a #LrgDemoManager
 * @url: (nullable): the purchase URL
 *
 * Sets the URL to redirect users for purchasing the full game.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_set_purchase_url (LrgDemoManager *self,
                                        const gchar    *url);

/**
 * lrg_demo_manager_get_purchase_url:
 * @self: a #LrgDemoManager
 *
 * Gets the purchase URL.
 *
 * Returns: (transfer none) (nullable): the purchase URL
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_demo_manager_get_purchase_url (LrgDemoManager *self);

/**
 * lrg_demo_manager_open_purchase_url:
 * @self: a #LrgDemoManager
 * @error: (optional): return location for error
 *
 * Opens the purchase URL in the system browser.
 *
 * Returns: %TRUE if URL was opened successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_open_purchase_url (LrgDemoManager  *self,
                                             GError         **error);

/* ==========================================================================
 * Upgrade Detection
 * ========================================================================== */

/**
 * lrg_demo_manager_set_upgrade_check_func:
 * @self: a #LrgDemoManager
 * @func: (nullable): function to check for upgrade
 * @user_data: (nullable): data passed to @func
 *
 * Sets a function to check if the user has upgraded to the full version.
 *
 * The function should return %TRUE if the full version is now available.
 * This can be used to detect Steam license changes, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_demo_manager_set_upgrade_check_func (LrgDemoManager *self,
                                              gboolean (*func) (gpointer),
                                              gpointer        user_data);

/**
 * lrg_demo_manager_check_upgrade:
 * @self: a #LrgDemoManager
 *
 * Checks if user has upgraded to the full version.
 *
 * If upgraded, automatically disables demo mode and emits
 * demo-ended with LRG_DEMO_END_REASON_UPGRADED.
 *
 * Returns: %TRUE if upgraded
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_demo_manager_check_upgrade (LrgDemoManager *self);

G_END_DECLS
