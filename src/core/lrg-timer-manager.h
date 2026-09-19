/* lrg-timer-manager.h - Frame-driven gameplay timers
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

#define LRG_TYPE_TIMER_MANAGER (lrg_timer_manager_get_type ())
LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgTimerManager, lrg_timer_manager, LRG, TIMER_MANAGER, GObject)

/**
 * lrg_timer_manager_new:
 *
 * Creates an isolated scheduler, driven explicitly by frame deltas. All calls
 * and timeout handlers must run on the same thread. No main loop is required.
 *
 * Returns: (transfer full): a new scheduler
 */
LRG_AVAILABLE_IN_ALL
LrgTimerManager *lrg_timer_manager_new (void);

/**
 * lrg_timer_manager_add:
 * @self: a scheduler
 * @delay: finite, nonnegative delay in seconds; positive for repeating timers
 * @repeat: whether to repeat at @delay intervals
 * @unscaled: whether to use the unscaled delta supplied to update
 *
 * Schedules a timeout. Zero-delay one-shots fire on the next update with a
 * positive delta in their clock domain. IDs are local to this manager, never
 * reused, and remain invalid after cancellation or completion, including clear.
 *
 * Returns: a nonzero timer ID, or zero for an invalid delay or exhausted IDs
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_timer_manager_add (LrgTimerManager *self,
                               gdouble          delay,
                               gboolean         repeat,
                               gboolean         unscaled);

/**
 * lrg_timer_manager_cancel:
 * @self: a scheduler
 * @id: a timer ID
 *
 * Removes a timer without emitting a timeout.
 *
 * Returns: whether the timer existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timer_manager_cancel (LrgTimerManager *self,
                                   guint64          id);

/**
 * lrg_timer_manager_set_paused:
 * @self: a scheduler
 * @id: a timer ID
 * @paused: whether to freeze the timer's remaining time
 *
 * A handler may pause another due timer before its timeout is delivered. Its
 * remaining time stays zero until resumed and advanced with a positive delta.
 *
 * Returns: whether the timer existed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timer_manager_set_paused (LrgTimerManager *self,
                                       guint64          id,
                                       gboolean         paused);

/**
 * lrg_timer_manager_get_remaining:
 * @self: a scheduler
 * @id: a timer ID
 *
 * Returns: seconds until timeout, or -1 if the timer does not exist
 */
LRG_AVAILABLE_IN_ALL
gdouble lrg_timer_manager_get_remaining (LrgTimerManager *self,
                                         guint64          id);

/**
 * lrg_timer_manager_get_count:
 * @self: a scheduler
 *
 * Returns: number of scheduled timers, including paused timers
 */
LRG_AVAILABLE_IN_ALL
guint lrg_timer_manager_get_count (LrgTimerManager *self);

/**
 * lrg_timer_manager_clear:
 * @self: a scheduler
 *
 * Cancels all timers, including pending timeouts in the current update.
 */
LRG_AVAILABLE_IN_ALL
void lrg_timer_manager_clear (LrgTimerManager *self);

/**
 * lrg_timer_manager_update:
 * @self: a scheduler
 * @scaled_delta: finite, nonnegative gameplay seconds elapsed
 * @unscaled_delta: finite, nonnegative real seconds elapsed
 *
 * Advances timers present at entry, then emits timeout in registration order
 * for due timers that have not been cancelled or paused. New timers wait until
 * the next update. One-shots are removed and repeats rearmed before emission.
 * Repeats emit at most once per update, coalescing missed intervals while
 * retaining their phase. Zero delta freezes the corresponding clock domain.
 * Handlers may add, cancel, clear, or pause timers safely. Recursive updates
 * are rejected. Invalid deltas leave all timers unchanged.
 *
 * Returns: %TRUE if advanced, %FALSE for invalid deltas or recursive calls
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_timer_manager_update (LrgTimerManager *self,
                                   gdouble          scaled_delta,
                                   gdouble          unscaled_delta);

G_END_DECLS
