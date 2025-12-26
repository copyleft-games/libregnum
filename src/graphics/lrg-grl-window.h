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
 * lrg_grl_window_get_grl_window:
 * @self: an #LrgGrlWindow
 *
 * Get the underlying GrlWindow for advanced usage.
 *
 * Returns: (transfer none): the underlying #GrlWindow
 */
LRG_AVAILABLE_IN_ALL
GrlWindow * lrg_grl_window_get_grl_window (LrgGrlWindow *self);

G_END_DECLS
