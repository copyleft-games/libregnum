/* lrg-2d-surface.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Concrete orthographic, pixel-exact #LrgFrameSurface backed by a #GrlWindow
 * rendering to the default framebuffer. This is the v1 (LRG_RENDER_MODE_2D)
 * implementation; it also implements #LrgTextRenderer.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-frame-surface.h"

G_BEGIN_DECLS

#define LRG_TYPE_2D_SURFACE (lrg_2d_surface_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (Lrg2DSurface, lrg_2d_surface, LRG, 2D_SURFACE, LrgFrameSurface)

/**
 * lrg_2d_surface_new:
 * @width: initial window width in pixels
 * @height: initial window height in pixels
 * @title: (nullable): window title
 *
 * Creates the 2D surface and its #GrlWindow (which initialises raylib's single
 * process window -- only one surface may exist at a time).
 *
 * Returns: (transfer full): a new #Lrg2DSurface
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
Lrg2DSurface * lrg_2d_surface_new (gint         width,
                                   gint         height,
                                   const gchar *title);

/**
 * lrg_2d_surface_get_window:
 * @self: a #Lrg2DSurface
 *
 * Returns: (transfer none): the underlying #GrlWindow (for input polling and
 *   native integration by the embedding display backend)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlWindow * lrg_2d_surface_get_window (Lrg2DSurface *self);

G_END_DECLS
