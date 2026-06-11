/* lrg-reel-gpu-renderer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgReelGpuRenderer - engine-native GPU capture for reels.
 *
 * Opens a hidden GL window and an off-screen framebuffer (#GrlRenderTexture),
 * then captures each reel frame by running a user draw callback into the FBO and
 * reading it back to a #GrlImage.  Unlike the headless CPU renderer this needs a
 * GL context (a display), so it is opt-in and display-gated.  The captured
 * frames feed the same #LrgReelExporter sinks as the CPU path — so a 3D scene
 * can be encoded to MP4, or its frames reused as image clips inside a CPU reel.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-reel-exporter.h"

G_BEGIN_DECLS

#define LRG_TYPE_REEL_GPU_RENDERER (lrg_reel_gpu_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgReelGpuRenderer, lrg_reel_gpu_renderer, LRG, REEL_GPU_RENDERER, GObject)

/**
 * LrgReelGpuDrawFunc:
 * @self: the #LrgReelGpuRenderer
 * @frame: the frame index being captured.
 * @user_data: the data passed to lrg_reel_gpu_renderer_set_draw_func().
 *
 * Issues the GL draw calls for @frame (camera begin/end, scene/drawables, etc).
 * Called inside an active off-screen framebuffer that has already been cleared.
 * Must be deterministic for reproducible output.
 *
 * Since: 1.0
 */
typedef void (*LrgReelGpuDrawFunc) (LrgReelGpuRenderer *self,
                                    gint                frame,
                                    gpointer            user_data);

/**
 * LrgReelGpuOverlayFunc:
 * @self: the #LrgReelGpuRenderer
 * @frame: the frame index being captured.
 * @user_data: the data passed to lrg_reel_gpu_renderer_set_overlay_func().
 *
 * Issues 2D screen-space draw calls (text, rectangles, etc) for @frame.
 * Called inside the active capture framebuffer *after* the world has been
 * color-graded and bloomed, so overlays composite on top of the final image.
 * Must be deterministic for reproducible output.
 *
 * Since: 1.0
 */
typedef void (*LrgReelGpuOverlayFunc) (LrgReelGpuRenderer *self,
                                       gint                frame,
                                       gpointer            user_data);

/**
 * lrg_reel_gpu_renderer_is_available:
 *
 * Returns: %TRUE if a display (and thus a GL context) appears to be available
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_gpu_renderer_is_available (void);

/**
 * lrg_reel_gpu_renderer_new:
 * @width: frame width.
 * @height: frame height.
 * @fps: frames per second.
 * @duration_in_frames: total number of frames.
 * @error: (nullable): return location for a #GError.
 *
 * Opens a hidden GL window and an off-screen framebuffer of @width x @height.
 *
 * Returns: (transfer full) (nullable): a new renderer, or %NULL (e.g. no display)
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
LrgReelGpuRenderer *
lrg_reel_gpu_renderer_new (gint      width,
                           gint      height,
                           gdouble   fps,
                           gint      duration_in_frames,
                           GError  **error);

/**
 * lrg_reel_gpu_renderer_set_draw_func:
 * @self: a #LrgReelGpuRenderer
 * @func: (scope notified) (nullable): the draw callback.
 * @user_data: (closure func): user data for @func.
 * @destroy: (nullable): destroy notifier for @user_data.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gpu_renderer_set_draw_func (LrgReelGpuRenderer *self,
                                     LrgReelGpuDrawFunc  func,
                                     gpointer            user_data,
                                     GDestroyNotify      destroy);

/**
 * lrg_reel_gpu_renderer_set_clear_color:
 * @self: a #LrgReelGpuRenderer
 * @color: the framebuffer clear color.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gpu_renderer_set_clear_color (LrgReelGpuRenderer *self,
                                       const GrlColor     *color);

/**
 * lrg_reel_gpu_renderer_set_overlay_func:
 * @self: a #LrgReelGpuRenderer
 * @func: (scope notified) (nullable): the overlay draw callback.
 * @user_data: (closure func): user data for @func.
 * @destroy: (nullable): destroy notifier for @user_data.
 *
 * Installs a 2D overlay callback drawn on top of the graded/bloomed world.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gpu_renderer_set_overlay_func (LrgReelGpuRenderer    *self,
                                        LrgReelGpuOverlayFunc  func,
                                        gpointer               user_data,
                                        GDestroyNotify         destroy);

/**
 * lrg_reel_gpu_renderer_set_grade:
 * @self: a #LrgReelGpuRenderer
 * @contrast: contrast multiplier around 0.5 (1.0 = identity).
 * @brightness: additive exposure offset (0.0 = identity).
 * @tint_r: red multiply tint (1.0 = identity).
 * @tint_g: green multiply tint (1.0 = identity).
 * @tint_b: blue multiply tint (1.0 = identity).
 *
 * Sets the per-frame GPU color-grade applied when transferring the world into
 * the capture framebuffer. The pass always runs (identity values = passthrough).
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gpu_renderer_set_grade (LrgReelGpuRenderer *self,
                                 gdouble             contrast,
                                 gdouble             brightness,
                                 gdouble             tint_r,
                                 gdouble             tint_g,
                                 gdouble             tint_b);

/**
 * lrg_reel_gpu_renderer_set_bloom:
 * @self: a #LrgReelGpuRenderer
 * @intensity: bloom additive strength (0.0 disables the bloom passes).
 * @threshold: luminance threshold (0..1) for the bright-pass.
 *
 * Sets the per-frame GPU bloom. When @intensity is 0 the bloom passes are
 * skipped entirely.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void
lrg_reel_gpu_renderer_set_bloom (LrgReelGpuRenderer *self,
                                 gdouble             intensity,
                                 gdouble             threshold);

/**
 * lrg_reel_gpu_renderer_capture_frame:
 * @self: a #LrgReelGpuRenderer
 * @frame: the frame index to capture.
 *
 * Renders @frame into the off-screen framebuffer and reads it back.
 *
 * Returns: (transfer full) (nullable): the captured #GrlImage
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
GrlImage *
lrg_reel_gpu_renderer_capture_frame (LrgReelGpuRenderer *self,
                                     gint                frame);

/**
 * lrg_reel_gpu_renderer_render_to_exporter:
 * @self: a #LrgReelGpuRenderer
 * @exporter: the #LrgReelExporter sink.
 * @error: (nullable): return location for a #GError.
 *
 * Captures every frame and feeds it to @exporter.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
gboolean
lrg_reel_gpu_renderer_render_to_exporter (LrgReelGpuRenderer *self,
                                          LrgReelExporter    *exporter,
                                          GError            **error);

LRG_AVAILABLE_IN_ALL
gint    lrg_reel_gpu_renderer_get_width  (LrgReelGpuRenderer *self);
LRG_AVAILABLE_IN_ALL
gint    lrg_reel_gpu_renderer_get_height (LrgReelGpuRenderer *self);
LRG_AVAILABLE_IN_ALL
gdouble lrg_reel_gpu_renderer_get_fps    (LrgReelGpuRenderer *self);

G_END_DECLS
