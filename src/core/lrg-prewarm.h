/* lrg-prewarm.h - A queue of GPU-resource work run ahead of first use
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-prewarmable.h"

G_BEGIN_DECLS

#define LRG_TYPE_PREWARM (lrg_prewarm_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPrewarm, lrg_prewarm, LRG, PREWARM, GObject)

/**
 * LrgPrewarmFunc:
 * @user_data: (closure): the data given to lrg_prewarm_add_func()
 *
 * One piece of prewarm work: build some GPU resources now. Runs on the GL
 * thread with a window open.
 *
 * Returns: %TRUE when the work is finished, %FALSE to be called again on
 *   a later turn (work split into steps; it must finish eventually)
 */
typedef gboolean (*LrgPrewarmFunc) (gpointer user_data);

/**
 * lrg_prewarm_new:
 *
 * Creates an empty prewarm queue. Most code uses the shared one,
 * lrg_prewarm_get_default() (also lrg_engine_get_prewarm()), which the
 * engine's renderers add themselves to.
 *
 * Returns: (transfer full): a new #LrgPrewarm
 */
LRG_AVAILABLE_IN_ALL
LrgPrewarm *lrg_prewarm_new (void);

/**
 * lrg_prewarm_get_default:
 *
 * Gets the process-wide prewarm queue: the one lrg_prewarm_track() adds
 * to, lrg_engine_get_prewarm() returns and the template loading state
 * drains. The first call (made by the engine's constructor) fixes the
 * thread whose new objects are tracked.
 *
 * Returns: (transfer none): the default #LrgPrewarm
 */
LRG_AVAILABLE_IN_ALL
LrgPrewarm *lrg_prewarm_get_default (void);

/**
 * lrg_prewarm_track:
 * @object: an #LrgPrewarmable, usually one being constructed
 *
 * Adds @object to the default queue (see lrg_prewarm_get_default()) when
 * that queue's #LrgPrewarm:auto-track is on and this is the thread that
 * created it (objects built on worker threads are not touched by the GL
 * thread behind their back). The engine's renderers call this from their
 * instance init; an #LrgPrewarmable of a game may too. The queue holds a
 * weak reference: an object freed before its turn is skipped.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_track (LrgPrewarmable *object);

/**
 * lrg_prewarm_add_object:
 * @self: an #LrgPrewarm
 * @object: the object to warm
 *
 * Queues lrg_prewarmable_prewarm() of @object. The queue holds a weak
 * reference: if @object is freed before its turn, the entry is dropped.
 * Adding an object twice (or one that is warm) only costs a no-op call.
 * May be called from any thread.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_add_object (LrgPrewarm     *self,
                             LrgPrewarmable *object);

/**
 * lrg_prewarm_add_func:
 * @self: an #LrgPrewarm
 * @name: (nullable): a name for debug output
 * @func: (scope notified) (closure user_data) (destroy destroy): the work
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data once @func finished (or the
 *   queue was cleared)
 *
 * Queues a piece of work, such as loading a game's own shaders. It runs
 * on the GL thread when the queue is run; returning %FALSE puts it back at
 * the end of the queue to run again. May be called from any thread.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_add_func (LrgPrewarm     *self,
                           const gchar    *name,
                           LrgPrewarmFunc  func,
                           gpointer        user_data,
                           GDestroyNotify  destroy);

/**
 * lrg_prewarm_run:
 * @self: an #LrgPrewarm
 * @budget_us: the time to spend, in microseconds; 0 or less runs a
 *   single entry
 *
 * Runs queued work in order until @budget_us has passed or the queue is
 * empty. At least one entry runs per call, and an entry is never cut
 * short, so one long shader compile can overrun the budget. Call it once
 * per frame from a loading screen to keep the screen animating. Must be
 * called on the GL thread; without an open window it does nothing (the
 * queue is kept for later) and returns %FALSE.
 *
 * Returns: %TRUE when work remains, %FALSE when the queue is empty or no
 *   window is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prewarm_run (LrgPrewarm *self,
                          gint64      budget_us);

/**
 * lrg_prewarm_run_all:
 * @self: an #LrgPrewarm
 *
 * Runs the whole queue now, blocking until it is empty (including work
 * queued while it runs). Must be called on the GL thread; without an open
 * window it does nothing.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_run_all (LrgPrewarm *self);

/**
 * lrg_prewarm_get_pending:
 * @self: an #LrgPrewarm
 *
 * Gets the number of queued entries, including tracked objects that are
 * already warm or freed (they cost nothing when their turn comes).
 *
 * Returns: the queue length
 */
LRG_AVAILABLE_IN_ALL
guint lrg_prewarm_get_pending (LrgPrewarm *self);

/**
 * lrg_prewarm_get_completed:
 * @self: an #LrgPrewarm
 *
 * Gets the number of entries run to completion since @self was created.
 *
 * Returns: the count of finished entries
 */
LRG_AVAILABLE_IN_ALL
guint64 lrg_prewarm_get_completed (LrgPrewarm *self);

/**
 * lrg_prewarm_get_elapsed_us:
 * @self: an #LrgPrewarm
 *
 * Gets the total time spent running entries since @self was created.
 *
 * Returns: microseconds spent in queued work
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_prewarm_get_elapsed_us (LrgPrewarm *self);

/**
 * lrg_prewarm_clear:
 * @self: an #LrgPrewarm
 *
 * Drops every queued entry without running it (destroying the data of
 * queued functions).
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_clear (LrgPrewarm *self);

/**
 * lrg_prewarm_get_auto_track:
 * @self: an #LrgPrewarm
 *
 * Gets whether lrg_prewarm_track() adds objects to @self (only the
 * default queue is ever tracked into).
 *
 * Returns: %TRUE when new renderers are tracked
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_prewarm_get_auto_track (LrgPrewarm *self);

/**
 * lrg_prewarm_set_auto_track:
 * @self: an #LrgPrewarm
 * @auto_track: whether new renderers add themselves
 *
 * Sets #LrgPrewarm:auto-track. Off, objects are only queued by
 * lrg_prewarm_add_object().
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_set_auto_track (LrgPrewarm *self,
                                 gboolean    auto_track);

/**
 * lrg_prewarm_begin_hidden_draw:
 *
 * Starts a hidden draw: everything drawn until
 * lrg_prewarm_end_hidden_draw() goes through the GPU pipeline (so the
 * driver compiles the shader variants its draw state needs, which many
 * drivers, llvmpipe among them, only do at a program's first draw) but
 * touches no pixel: the scissor rectangle is empty. Draw the game's
 * scene once this way behind a loading screen (see the
 * #LrgTemplateLoadingState::prewarm-draw signal). Flushes what was queued
 * before; must not be nested inside scissor mode. Does nothing without a
 * window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_begin_hidden_draw (void);

/**
 * lrg_prewarm_end_hidden_draw:
 *
 * Ends a hidden draw started with lrg_prewarm_begin_hidden_draw(): flushes
 * the draws made in it and turns the scissor test off.
 */
LRG_AVAILABLE_IN_ALL
void lrg_prewarm_end_hidden_draw (void);

G_END_DECLS
