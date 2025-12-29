/* lrg-tween-parallel.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Parallel tween group that plays all tweens simultaneously.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-tween-group.h"

G_BEGIN_DECLS

#define LRG_TYPE_TWEEN_PARALLEL (lrg_tween_parallel_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTweenParallel, lrg_tween_parallel, LRG, TWEEN_PARALLEL, LrgTweenGroup)

/**
 * lrg_tween_parallel_new:
 *
 * Creates a new parallel tween group.
 * All tweens added to a parallel group play simultaneously.
 *
 * Returns: (transfer full): A new #LrgTweenParallel
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenParallel *  lrg_tween_parallel_new              (void);

/**
 * lrg_tween_parallel_add:
 * @self: A #LrgTweenParallel
 * @tween: (transfer none): The tween to add
 *
 * Adds a tween to the parallel group.
 * This is equivalent to lrg_tween_group_add_tween().
 *
 * Returns: (transfer none): @self for method chaining
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgTweenParallel *  lrg_tween_parallel_add              (LrgTweenParallel    *self,
                                                         LrgTweenBase        *tween);

/**
 * lrg_tween_parallel_get_finished_count:
 * @self: A #LrgTweenParallel
 *
 * Gets the number of tweens that have finished.
 *
 * Returns: The number of finished tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tween_parallel_get_finished_count (LrgTweenParallel  *self);

/**
 * lrg_tween_parallel_get_running_count:
 * @self: A #LrgTweenParallel
 *
 * Gets the number of tweens that are still running.
 *
 * Returns: The number of running tweens
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
guint               lrg_tween_parallel_get_running_count  (LrgTweenParallel  *self);

G_END_DECLS
