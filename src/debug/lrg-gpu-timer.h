/* lrg-gpu-timer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgGpuTimer: GPU section timing with GL timestamp queries.
 *
 * Every lrg_gpu_timer_begin() / lrg_gpu_timer_end() pair issues two
 * glQueryCounter(GL_TIMESTAMP) queries.  Queries are kept per frame in
 * a ring of LRG_GPU_TIMER_FRAMES_IN_FLIGHT slots and only read once the
 * driver reports them available, so the timer never waits for the GPU:
 * results arrive two or three frames after the work was submitted, and a
 * slot whose results are still pending when it comes round again is
 * dropped (counted by lrg_gpu_timer_get_dropped()).  GPU timestamps are
 * mapped onto the CPU's monotonic clock with a GL_TIMESTAMP read at every
 * frame start, so GPU events line up with CPU sections in a timeline.
 *
 * #LrgProfiler owns one when its GPU lane is enabled and feeds the results
 * into its frames; the timer can also be used on its own.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"

G_BEGIN_DECLS

/**
 * LRG_GPU_TIMER_FRAMES_IN_FLIGHT:
 *
 * Frames of queries the timer keeps before reusing a slot.
 */
#define LRG_GPU_TIMER_FRAMES_IN_FLIGHT (4)

/**
 * LRG_GPU_TIMER_MAX_SECTIONS:
 *
 * At most this many sections are timed per frame; further ones are
 * ignored (and counted as dropped).
 */
#define LRG_GPU_TIMER_MAX_SECTIONS (512)

/**
 * LrgGpuTimerResultFunc:
 * @frame_index: the frame number passed to lrg_gpu_timer_begin_frame()
 * @name: the section name (interned)
 * @depth: nesting depth (0 = top level)
 * @start_us: start on the monotonic clock (g_get_monotonic_time() base)
 * @duration_us: GPU duration in microseconds
 * @user_data: data passed to lrg_gpu_timer_collect()
 *
 * Receives one finished GPU section.
 */
typedef void (*LrgGpuTimerResultFunc) (guint64      frame_index,
                                       const gchar *name,
                                       guint        depth,
                                       gint64       start_us,
                                       gint64       duration_us,
                                       gpointer     user_data);

#define LRG_TYPE_GPU_TIMER (lrg_gpu_timer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGpuTimer, lrg_gpu_timer, LRG, GPU_TIMER, GObject)

/**
 * lrg_gpu_timer_new:
 *
 * Creates a GPU timer.  GL is touched only on the first
 * lrg_gpu_timer_begin_frame() (or lrg_gpu_timer_is_supported()).
 *
 * Returns: (transfer full): a new #LrgGpuTimer
 */
LRG_AVAILABLE_IN_ALL
LrgGpuTimer * lrg_gpu_timer_new (void);

/**
 * lrg_gpu_timer_is_supported:
 * @self: an #LrgGpuTimer
 *
 * Checks for a current GL context with timestamp queries (GL 3.3 or
 * ARB_timer_query).  Resolves the entry points on the first call.
 *
 * Returns: %TRUE when sections can be timed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_gpu_timer_is_supported (LrgGpuTimer *self);

/**
 * lrg_gpu_timer_begin_frame:
 * @self: an #LrgGpuTimer
 * @frame_index: a number identifying the frame (results carry it)
 *
 * Starts a frame: claims the next query slot (dropping its results if
 * they never arrived) and calibrates the GPU clock against the CPU's.
 * Results of earlier frames are not read here: call
 * lrg_gpu_timer_collect().
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_timer_begin_frame (LrgGpuTimer *self,
                                guint64      frame_index);

/**
 * lrg_gpu_timer_end_frame:
 * @self: an #LrgGpuTimer
 *
 * Ends the frame.  Sections still open are closed here.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_timer_end_frame (LrgGpuTimer *self);

/**
 * lrg_gpu_timer_begin:
 * @self: an #LrgGpuTimer
 * @name: the section name
 *
 * Begins a GPU section: flushes rlgl's batch and issues a timestamp
 * query.  Sections nest.  Ignored outside a frame or without GL.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_timer_begin (LrgGpuTimer *self,
                          const gchar *name);

/**
 * lrg_gpu_timer_end:
 * @self: an #LrgGpuTimer
 *
 * Ends the innermost open GPU section.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_timer_end (LrgGpuTimer *self);

/**
 * lrg_gpu_timer_collect:
 * @self: an #LrgGpuTimer
 * @func: (scope call) (nullable): receives each finished section
 * @user_data: data for @func
 *
 * Reads the results of every finished frame whose queries are all
 * available (without waiting) and hands each section to @func.
 *
 * Returns: the number of frames collected
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_timer_collect (LrgGpuTimer           *self,
                             LrgGpuTimerResultFunc  func,
                             gpointer               user_data);

/**
 * lrg_gpu_timer_get_last_frame_us:
 * @self: an #LrgGpuTimer
 *
 * Gets the GPU time of the last collected frame (sum of its top-level
 * sections).
 *
 * Returns: microseconds, or -1 before the first result
 */
LRG_AVAILABLE_IN_ALL
gint64 lrg_gpu_timer_get_last_frame_us (LrgGpuTimer *self);

/**
 * lrg_gpu_timer_get_dropped:
 * @self: an #LrgGpuTimer
 *
 * Gets how many sections were dropped: results still pending when their
 * slot was reused, or sections over %LRG_GPU_TIMER_MAX_SECTIONS.
 *
 * Returns: the dropped section count
 */
LRG_AVAILABLE_IN_ALL
guint lrg_gpu_timer_get_dropped (LrgGpuTimer *self);

/**
 * lrg_gpu_timer_release:
 * @self: an #LrgGpuTimer
 *
 * Deletes the GL query objects (call while the context is still current,
 * before closing the window).  Pending results are lost; the timer can
 * be used again afterwards.
 */
LRG_AVAILABLE_IN_ALL
void lrg_gpu_timer_release (LrgGpuTimer *self);

G_END_DECLS
