/* lrg-paint-renderer.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Draws #LrgPaintList frames with one instanced signed-distance-field
 * shader.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-paint-list.h"

G_BEGIN_DECLS

#define LRG_TYPE_PAINT_RENDERER (lrg_paint_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgPaintRenderer, lrg_paint_renderer, LRG, PAINT_RENDERER, GObject)

/**
 * lrg_paint_renderer_new:
 *
 * Creates a renderer. GPU resources are created lazily on the first
 * lrg_paint_renderer_render() call made with a GL context current, so
 * the renderer can be created before the window opens.
 *
 * Returns: (transfer full): a new #LrgPaintRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgPaintRenderer * lrg_paint_renderer_new (void);

/**
 * lrg_paint_renderer_get_default:
 *
 * Gets the process-wide renderer shared by #LrgCanvas and the
 * immediate-mode GUI backend. Sharing one renderer shares its shader
 * and vertex buffers.
 *
 * Returns: (transfer none): the default #LrgPaintRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgPaintRenderer * lrg_paint_renderer_get_default (void);

/**
 * lrg_paint_renderer_render:
 * @self: an #LrgPaintRenderer
 * @list: the frame to draw
 *
 * Draws @list in order on top of whatever is in the current target,
 * using the current raylib camera/transform. Pending raylib draws are
 * flushed first so ordering with immediate-mode drawing is preserved.
 * Does nothing (and returns %FALSE) when no window is open.
 *
 * Returns: %TRUE if the list was drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_renderer_render (LrgPaintRenderer *self,
                                    LrgPaintList     *list);

/**
 * lrg_paint_renderer_get_draw_calls:
 * @self: an #LrgPaintRenderer
 *
 * Returns: instanced draw calls issued by the last render, for
 *   profiling (callbacks are not counted)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_renderer_get_draw_calls (LrgPaintRenderer *self);

/**
 * lrg_paint_renderer_get_backdrop_passes:
 * @self: an #LrgPaintRenderer
 *
 * Number of backdrop captures the last render made: one per
 * %LRG_PAINT_COMMAND_BACKDROP batch, each a copy of part of the target
 * plus its blur chain. Zero for frames without blur or glass, which
 * then cost exactly what they did before materials existed.
 *
 * Returns: backdrop captures issued by the last render
 */
LRG_AVAILABLE_IN_ALL
guint lrg_paint_renderer_get_backdrop_passes (LrgPaintRenderer *self);

/**
 * lrg_paint_renderer_is_available:
 *
 * Returns: %TRUE when a window (and so a GL context) is open
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_paint_renderer_is_available (void);

G_END_DECLS
