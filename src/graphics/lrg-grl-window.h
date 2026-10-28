/* lrg-grl-window.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Graylib window backend implementation.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-window.h"

G_BEGIN_DECLS

#define LRG_TYPE_GRL_WINDOW (lrg_grl_window_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgGrlWindow, lrg_grl_window, LRG, GRL_WINDOW, LrgWindow)

/**
 * lrg_grl_window_new:
 * @width: initial window width
 * @height: initial window height
 * @title: window title
 *
 * Create a new graylib window.
 *
 * Returns: (transfer full): a new #LrgGrlWindow
 */
LRG_AVAILABLE_IN_ALL
LrgGrlWindow * lrg_grl_window_new (gint         width,
                                   gint         height,
                                   const gchar *title);

/**
 * lrg_grl_window_toggle_fullscreen:
 * @self: an #LrgGrlWindow
 *
 * Toggle fullscreen mode.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grl_window_toggle_fullscreen (LrgGrlWindow *self);

/**
 * lrg_grl_window_set_vsync:
 * @self: an #LrgGrlWindow
 * @vsync: whether to enable vsync
 *
 * Enable or disable vertical sync.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grl_window_set_vsync (LrgGrlWindow *self,
                               gboolean      vsync);

/**
 * lrg_grl_window_get_vsync:
 * @self: an #LrgGrlWindow
 *
 * Check if vsync is enabled.
 *
 * Returns: %TRUE if vsync is enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_grl_window_get_vsync (LrgGrlWindow *self);

/**
 * lrg_grl_window_set_max_frames_in_flight:
 * @self: an #LrgGrlWindow
 * @frames: frames in flight, 0 (driver default) to %GRL_FRAME_SYNC_MAX_FRAMES
 *
 * Bound the frames in flight with GL fences (graylib's frame sync).
 */
LRG_AVAILABLE_IN_ALL
void lrg_grl_window_set_max_frames_in_flight (LrgGrlWindow *self,
                                              guint         frames);

/**
 * lrg_grl_window_get_max_frames_in_flight:
 * @self: an #LrgGrlWindow
 *
 * Returns: the frames-in-flight bound (0: driver default)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_grl_window_get_max_frames_in_flight (LrgGrlWindow *self);

/**
 * lrg_grl_window_resolve_max_frames_in_flight:
 * @requested: the bound the game asked for
 *
 * Apply the `LRG_MAX_FRAMES_IN_FLIGHT` environment override.
 *
 * Returns: the bound to use
 */
LRG_AVAILABLE_IN_ALL
guint lrg_grl_window_resolve_max_frames_in_flight (guint requested);

/**
 * lrg_grl_window_get_grl_window:
 * @self: an #LrgGrlWindow
 *
 * Get the underlying GrlWindow for advanced usage.
 *
 * Returns: (transfer none): the underlying #GrlWindow
 */
LRG_AVAILABLE_IN_ALL
GrlWindow * lrg_grl_window_get_grl_window (LrgGrlWindow *self);

/**
 * lrg_grl_window_set_present:
 * @self: an #LrgGrlWindow
 * @present: whether lrg_window_end_frame() swaps the window's buffers
 *
 * Turn presenting the window on or off.  While another display shows the
 * frames -- a VR headset, whose runtime paces the loop through
 * xrWaitFrame -- the desktop swap is wasted work at best; under a
 * compositor that does not show the window (gamescope with the headset
 * active) it can block the GL queue, and the semaphore the VR runtime waits
 * on in xrEndFrame sits behind it, hanging the frame loop.
 *
 * With presenting off, lrg_window_end_frame() flushes the active render
 * batch and polls window input once, but neither swaps nor sleeps to the
 * target FPS, and lrg_window_get_frame_time() reports the time measured
 * between those end_frame calls (0 for the first one) instead of raylib's.
 * Presenting is on by default.
 */
LRG_AVAILABLE_IN_ALL
void lrg_grl_window_set_present (LrgGrlWindow *self,
                                 gboolean      present);

/**
 * lrg_grl_window_get_present:
 * @self: an #LrgGrlWindow
 *
 * Whether lrg_window_end_frame() presents the window.
 *
 * Returns: %TRUE when the window is presented (the default)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_grl_window_get_present (LrgGrlWindow *self);

G_END_DECLS
