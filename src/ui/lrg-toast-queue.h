/* lrg-toast-queue.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgToastQueue: Shows LrgToast messages in a corner, one at a time or stacked.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"
#include "lrg-toast.h"

G_BEGIN_DECLS

#define LRG_TYPE_TOAST_QUEUE (lrg_toast_queue_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgToastQueue, lrg_toast_queue, LRG, TOAST_QUEUE, LrgContainer)

/**
 * lrg_toast_queue_new:
 *
 * Creates a queue in %LRG_TOAST_MODE_STACK mode showing up to three
 * toasts in the bottom-right corner. Add it to a canvas as the last
 * child; when it has no size of its own it covers its parent. It is
 * pointer-transparent: clicks only stop on the toasts themselves.
 *
 * Returns: (transfer full): a new #LrgToastQueue
 */
LRG_AVAILABLE_IN_ALL
LrgToastQueue * lrg_toast_queue_new (void);

/**
 * lrg_toast_queue_get_mode:
 * @self: an #LrgToastQueue
 *
 * Returns: how new toasts are shown
 */
LRG_AVAILABLE_IN_ALL
LrgToastMode lrg_toast_queue_get_mode (LrgToastQueue *self);

/**
 * lrg_toast_queue_set_mode:
 * @self: an #LrgToastQueue
 * @mode: %LRG_TOAST_MODE_REPLACE (a new toast pushes the current one
 *   out), %LRG_TOAST_MODE_SEQUENCE (one at a time, in order) or
 *   %LRG_TOAST_MODE_STACK (up to #LrgToastQueue:max-visible at once)
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_set_mode (LrgToastQueue *self,
                               LrgToastMode   mode);

/**
 * lrg_toast_queue_get_max_visible:
 * @self: an #LrgToastQueue
 *
 * Returns: the most toasts shown at once in stack mode
 */
LRG_AVAILABLE_IN_ALL
guint lrg_toast_queue_get_max_visible (LrgToastQueue *self);

/**
 * lrg_toast_queue_set_max_visible:
 * @self: an #LrgToastQueue
 * @max_visible: at least 1; extra toasts wait their turn
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_set_max_visible (LrgToastQueue *self,
                                      guint          max_visible);

/**
 * lrg_toast_queue_get_toast_halign:
 * @self: an #LrgToastQueue
 *
 * Returns: the horizontal corner / edge toasts sit at
 */
LRG_AVAILABLE_IN_ALL
LrgAlign lrg_toast_queue_get_toast_halign (LrgToastQueue *self);

/**
 * lrg_toast_queue_set_toast_halign:
 * @self: an #LrgToastQueue
 * @align: %LRG_ALIGN_START (left), %LRG_ALIGN_CENTER, %LRG_ALIGN_END
 *   (right) or %LRG_ALIGN_FILL (full width)
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_set_toast_halign (LrgToastQueue *self,
                                       LrgAlign       align);

/**
 * lrg_toast_queue_get_toast_valign:
 * @self: an #LrgToastQueue
 *
 * Returns: the vertical edge toasts stack from
 */
LRG_AVAILABLE_IN_ALL
LrgAlign lrg_toast_queue_get_toast_valign (LrgToastQueue *self);

/**
 * lrg_toast_queue_set_toast_valign:
 * @self: an #LrgToastQueue
 * @align: %LRG_ALIGN_START (top: newest on top, the stack grows
 *   down), %LRG_ALIGN_END or %LRG_ALIGN_FILL (bottom: newest at the
 *   bottom, the stack grows up) or %LRG_ALIGN_CENTER
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_set_toast_valign (LrgToastQueue *self,
                                       LrgAlign       align);

/**
 * lrg_toast_queue_get_auto_advance:
 * @self: an #LrgToastQueue
 *
 * Returns: whether the canvas frame clock advances the toasts
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_toast_queue_get_auto_advance (LrgToastQueue *self);

/**
 * lrg_toast_queue_set_auto_advance:
 * @self: an #LrgToastQueue
 * @auto_advance: %TRUE (default) to run time from the canvas frame
 *   clock (each lrg_canvas_update() advances the queue by its delta
 *   time); %FALSE to drive it only with lrg_toast_queue_advance()
 *   (paused games, manual control)
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_set_auto_advance (LrgToastQueue *self,
                                       gboolean       auto_advance);

/**
 * lrg_toast_queue_show:
 * @self: an #LrgToastQueue
 * @toast: (transfer none): a toast not shown anywhere else; the queue
 *   takes its own reference
 *
 * Shows @toast now or queues it, depending on the mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_show (LrgToastQueue *self,
                           LrgToast      *toast);

/**
 * lrg_toast_queue_advance:
 * @self: an #LrgToastQueue
 * @seconds: time that passed
 *
 * Runs the clocks of the visible toasts and the enter / leave / re-flow
 * springs, removes toasts that finished fading out and brings in
 * waiting ones. When #LrgToastQueue:auto-advance is on, the canvas
 * calls this every frame with its delta time (lrg_canvas_update()), so
 * an explicit call adds to the frame clock (skips ahead) rather than
 * replacing it; painting never advances time. Turn auto-advance off to
 * drive time only through this function.
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_advance (LrgToastQueue *self,
                              gdouble        seconds);

/**
 * lrg_toast_queue_clear:
 * @self: an #LrgToastQueue
 *
 * Dismisses every visible and waiting toast.
 */
LRG_AVAILABLE_IN_ALL
void lrg_toast_queue_clear (LrgToastQueue *self);

/**
 * lrg_toast_queue_get_n_visible:
 * @self: an #LrgToastQueue
 *
 * Returns: toasts on screen and not dismissed (toasts fading out after
 *   a dismissal are not counted)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_toast_queue_get_n_visible (LrgToastQueue *self);

/**
 * lrg_toast_queue_get_n_pending:
 * @self: an #LrgToastQueue
 *
 * Returns: toasts waiting to be shown
 */
LRG_AVAILABLE_IN_ALL
guint lrg_toast_queue_get_n_pending (LrgToastQueue *self);

/**
 * lrg_toast_queue_get_visible_toast:
 * @self: an #LrgToastQueue
 * @index: 0 for the oldest visible toast
 *
 * Returns: (transfer none) (nullable): a visible, not dismissed toast
 */
LRG_AVAILABLE_IN_ALL
LrgToast * lrg_toast_queue_get_visible_toast (LrgToastQueue *self,
                                              guint          index);

G_END_DECLS
