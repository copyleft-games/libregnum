/* lrg-reel-renderer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelRenderer - the offline, headless frame renderer.
 *
 * Renders a #LrgReel frame by frame onto a CPU #LrgImageCanvas (no GL context
 * required), compositing each active clip in z-order with its opacity, and
 * optionally streaming the result to an #LrgReelExporter.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_RENDERER (lrg_reel_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelRenderer, lrg_reel_renderer, LRG, REEL_RENDERER, GObject)

/**
 * LrgReelProgressFunc:
 * @frame: the frame just rendered (0-based).
 * @total: the total number of frames.
 * @user_data: user data from lrg_reel_renderer_set_progress_callback().
 *
 * Progress callback invoked once per frame during
 * lrg_reel_renderer_render_to_exporter().
 *
 * Since: 1.0
 */
typedef void (*LrgReelProgressFunc) (guint    frame,
                                     guint    total,
                                     gpointer user_data);

/**
 * lrg_reel_renderer_new:
 * @reel: (transfer none): the #LrgReel to render.
 *
 * Creates a renderer for @reel.  The renderer holds a reference to @reel.
 *
 * Returns: (transfer full): a new #LrgReelRenderer
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelRenderer *
lrg_reel_renderer_new (LrgReel *reel);

/**
 * lrg_reel_renderer_set_background:
 * @self: a #LrgReelRenderer
 * @background: (nullable): the clear color, or %NULL for transparent.
 *
 * Sets the color the canvas is cleared to before each frame's clips are drawn.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_renderer_set_background (LrgReelRenderer *self,
                                  const GrlColor  *background);

/**
 * lrg_reel_renderer_render_frame:
 * @self: a #LrgReelRenderer
 * @frame: the frame to render.
 *
 * Renders a single frame and returns it as a newly allocated image, owned by
 * the caller.
 *
 * Returns: (transfer full): the rendered RGBA8 #GrlImage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_reel_renderer_render_frame (LrgReelRenderer *self,
                                gint             frame);

/**
 * lrg_reel_renderer_get_canvas_image:
 * @self: a #LrgReelRenderer
 * @frame: the frame to render.
 *
 * Renders @frame into the renderer's reusable canvas and returns the live
 * backing image (owned by the renderer, valid until the next render).  Use
 * this to avoid a per-frame copy when feeding an exporter or a texture.
 *
 * Returns: (transfer none): the renderer's canvas #GrlImage for @frame
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_reel_renderer_get_canvas_image (LrgReelRenderer *self,
                                    gint             frame);

/**
 * lrg_reel_renderer_render_to_exporter:
 * @self: a #LrgReelRenderer
 * @exporter: the #LrgReelExporter sink.
 * @error: (nullable): return location for a #GError.
 *
 * Renders every frame of the reel (0 .. duration-1) and streams it to
 * @exporter via begin()/add_frame()/finish().
 *
 * Returns: %TRUE if the whole reel was exported successfully
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_renderer_render_to_exporter (LrgReelRenderer *self,
                                      LrgReelExporter *exporter,
                                      GError         **error);

/**
 * lrg_reel_renderer_set_progress_callback:
 * @self: a #LrgReelRenderer
 * @callback: (scope notified) (nullable): progress callback, or %NULL to clear.
 * @user_data: (closure callback): user data for @callback.
 * @destroy: (nullable): destroy notifier for @user_data.
 *
 * Sets a callback invoked once per frame during
 * lrg_reel_renderer_render_to_exporter().
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_renderer_set_progress_callback (LrgReelRenderer     *self,
                                         LrgReelProgressFunc  callback,
                                         gpointer             user_data,
                                         GDestroyNotify       destroy);

G_END_DECLS
