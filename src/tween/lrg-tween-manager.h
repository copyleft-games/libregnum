/* lrg-tween-manager.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Manages active tweens and updates them each frame.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-tween-base.h"
#include "lrg-tween.h"
#include "lrg-tween-sequence.h"
#include "lrg-tween-parallel.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN_MANAGER (lrg_tween_manager_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTweenManager, lrg_tween_manager, LRG, TWEEN_MANAGER, GObject)

/**
 * lrg_tween_manager_new:
 *
 * Creates a new tween manager.
 *
 * Returns: (transfer full): A new #LrgTweenManager
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenManager *   lrg_tween_manager_new               (void);

/* Tween lifecycle */

/**
 * lrg_tween_manager_add:
 * @self: A #LrgTweenManager
 * @tween: (transfer none): The tween to manage
 *
 * Adds a tween to the manager.
 * If the tween has auto-start enabled, it will be started immediately.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_add               (LrgTweenManager     *self,
                                                         LrgTweenBase        *tween);

/**
 * lrg_tween_manager_remove:
 * @self: A #LrgTweenManager
 * @tween: The tween to remove
 *
 * Removes a tween from the manager.
 *
 * Returns: %TRUE if the tween was found and removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tween_manager_remove            (LrgTweenManager     *self,
                                                         LrgTweenBase        *tween);

/**
 * lrg_tween_manager_clear:
 * @self: A #LrgTweenManager
 *
 * Removes all tweens from the manager.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_clear             (LrgTweenManager     *self);

/* Update */

/**
 * lrg_tween_manager_update:
 * @self: A #LrgTweenManager
 * @delta_time: Time elapsed since last update in seconds
 *
 * Updates all managed tweens. Should be called every frame.
 * Finished tweens are automatically removed unless configured otherwise.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_update            (LrgTweenManager     *self,
                                                         gfloat               delta_time);

/* Queries */

/**
 * lrg_tween_manager_get_tween_count:
 * @self: A #LrgTweenManager
 *
 * Gets the number of active tweens.
 *
 * Returns: The number of managed tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tween_manager_get_tween_count   (LrgTweenManager     *self);

/**
 * lrg_tween_manager_get_tweens:
 * @self: A #LrgTweenManager
 *
 * Gets the list of active tweens.
 *
 * Returns: (transfer none) (element-type LrgTweenBase): The tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GPtrArray *         lrg_tween_manager_get_tweens        (LrgTweenManager     *self);

/* Control */

/**
 * lrg_tween_manager_pause_all:
 * @self: A #LrgTweenManager
 *
 * Pauses all managed tweens.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_pause_all         (LrgTweenManager     *self);

/**
 * lrg_tween_manager_resume_all:
 * @self: A #LrgTweenManager
 *
 * Resumes all paused tweens.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_resume_all        (LrgTweenManager     *self);

/**
 * lrg_tween_manager_stop_all:
 * @self: A #LrgTweenManager
 *
 * Stops all managed tweens. They remain in the manager but are reset.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_stop_all          (LrgTweenManager     *self);

/* Convenience factory methods */

/**
 * lrg_tween_manager_create_tween:
 * @self: A #LrgTweenManager
 * @target: (transfer none): The target object
 * @property_name: The property to animate
 * @duration: Duration in seconds
 *
 * Creates and registers a new property tween.
 * The tween is added to the manager and will auto-start.
 *
 * Returns: (transfer none): The new tween (owned by manager)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTween *          lrg_tween_manager_create_tween      (LrgTweenManager     *self,
                                                         GObject             *target,
                                                         const gchar         *property_name,
                                                         gfloat               duration);

/**
 * lrg_tween_manager_create_sequence:
 * @self: A #LrgTweenManager
 *
 * Creates and registers a new tween sequence.
 * The sequence is added to the manager.
 *
 * Returns: (transfer none): The new sequence (owned by manager)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenSequence *  lrg_tween_manager_create_sequence   (LrgTweenManager     *self);

/**
 * lrg_tween_manager_create_parallel:
 * @self: A #LrgTweenManager
 *
 * Creates and registers a new parallel tween group.
 * The group is added to the manager.
 *
 * Returns: (transfer none): The new parallel group (owned by manager)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenParallel *  lrg_tween_manager_create_parallel   (LrgTweenManager     *self);

/* Settings */

/**
 * lrg_tween_manager_get_auto_remove_finished:
 * @self: A #LrgTweenManager
 *
 * Gets whether finished tweens are automatically removed.
 *
 * Returns: %TRUE if finished tweens are auto-removed
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean            lrg_tween_manager_get_auto_remove_finished (LrgTweenManager *self);

/**
 * lrg_tween_manager_set_auto_remove_finished:
 * @self: A #LrgTweenManager
 * @auto_remove: Whether to auto-remove finished tweens
 *
 * Sets whether finished tweens should be automatically removed
 * from the manager after completion.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_set_auto_remove_finished (LrgTweenManager *self,
                                                                gboolean         auto_remove);

/**
 * lrg_tween_manager_get_time_scale:
 * @self: A #LrgTweenManager
 *
 * Gets the global time scale applied to all tweens.
 *
 * Returns: The time scale multiplier (1.0 = normal speed)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gfloat              lrg_tween_manager_get_time_scale    (LrgTweenManager     *self);

/**
 * lrg_tween_manager_set_time_scale:
 * @self: A #LrgTweenManager
 * @scale: The time scale multiplier
 *
 * Sets a global time scale applied to all managed tweens.
 * Use 0.5 for half speed, 2.0 for double speed, etc.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void                lrg_tween_manager_set_time_scale    (LrgTweenManager     *self,
                                                         gfloat               scale);

G_END_DECLS
