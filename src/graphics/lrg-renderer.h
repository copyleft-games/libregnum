/* lrg-renderer.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Render management.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../lrg-enums.h"
#include "lrg-window.h"
#include "lrg-camera.h"
#include "lrg-drawable.h"

G_BEGIN_DECLS

#define LRG_TYPE_RENDERER (lrg_renderer_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgRenderer, lrg_renderer, LRG, RENDERER, GObject)

/**
 * LrgRendererClass:
 * @parent_class: Parent class
 * @begin_frame: Begin a new frame
 * @end_frame: End the current frame
 * @render_layer: Render a specific layer
 * @clear: Clear the screen with a color
 *
 * Class for render management. Can be subclassed for custom
 * rendering pipelines or post-processing effects.
 */
struct _LrgRendererClass
{
	GObjectClass parent_class;

	/*< public >*/

	/**
	 * LrgRendererClass::begin_frame:
	 * @self: an #LrgRenderer
	 *
	 * Begin a new frame. Called at the start of each render cycle.
	 */
	void (*begin_frame) (LrgRenderer *self);

	/**
	 * LrgRendererClass::end_frame:
	 * @self: an #LrgRenderer
	 *
	 * End the current frame. Called at the end of each render cycle.
	 */
	void (*end_frame) (LrgRenderer *self);

	/**
	 * LrgRendererClass::render_layer:
	 * @self: an #LrgRenderer
	 * @layer: the render layer
	 *
	 * Called when beginning to render a specific layer.
	 */
	void (*render_layer) (LrgRenderer    *self,
	                      LrgRenderLayer  layer);

	/**
	 * LrgRendererClass::clear:
	 * @self: an #LrgRenderer
	 * @color: the clear color
	 *
	 * Clear the screen with the specified color.
	 */
	void (*clear) (LrgRenderer *self,
	               GrlColor    *color);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_renderer_new:
 * @window: (transfer none): the window to render to
 *
 * Create a new renderer for the given window.
 *
 * Returns: (transfer full): a new #LrgRenderer
 */
LRG_AVAILABLE_IN_ALL
LrgRenderer * lrg_renderer_new (LrgWindow *window);

/* ==========================================================================
 * Frame Management
 * ========================================================================== */

/**
 * lrg_renderer_begin_frame:
 * @self: an #LrgRenderer
 *
 * Begin a new frame. Call this at the start of each render cycle.
 * This will call the window's begin_frame method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_begin_frame (LrgRenderer *self);

/**
 * lrg_renderer_end_frame:
 * @self: an #LrgRenderer
 *
 * End the current frame. Call this at the end of each render cycle.
 * This will call the window's end_frame method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_end_frame (LrgRenderer *self);

/**
 * lrg_renderer_clear:
 * @self: an #LrgRenderer
 * @color: the clear color
 *
 * Clear the screen with the specified color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_clear (LrgRenderer *self,
                         GrlColor    *color);

/* ==========================================================================
 * Camera Management
 * ========================================================================== */

/**
 * lrg_renderer_set_camera:
 * @self: an #LrgRenderer
 * @camera: (transfer none) (nullable): the camera to use for rendering
 *
 * Set the active camera for rendering. Pass %NULL to disable
 * camera-based rendering.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_set_camera (LrgRenderer *self,
                              LrgCamera   *camera);

/**
 * lrg_renderer_get_camera:
 * @self: an #LrgRenderer
 *
 * Get the active camera.
 *
 * Returns: (transfer none) (nullable): the active #LrgCamera, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgCamera * lrg_renderer_get_camera (LrgRenderer *self);

/* ==========================================================================
 * Layer-Based Rendering
 * ========================================================================== */

/**
 * lrg_renderer_begin_layer:
 * @self: an #LrgRenderer
 * @layer: the render layer to begin
 *
 * Begin rendering a specific layer. If a camera is set and
 * the layer is LRG_RENDER_LAYER_WORLD, the camera transform
 * will be applied.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_begin_layer (LrgRenderer    *self,
                               LrgRenderLayer  layer);

/**
 * lrg_renderer_end_layer:
 * @self: an #LrgRenderer
 *
 * End rendering the current layer. This will restore the
 * previous rendering state (e.g., end camera mode if applicable).
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_end_layer (LrgRenderer *self);

/**
 * lrg_renderer_get_current_layer:
 * @self: an #LrgRenderer
 *
 * Get the current render layer being rendered.
 *
 * Returns: the current #LrgRenderLayer
 */
LRG_AVAILABLE_IN_ALL
LrgRenderLayer lrg_renderer_get_current_layer (LrgRenderer *self);

/* ==========================================================================
 * Window Access
 * ========================================================================== */

/**
 * lrg_renderer_get_window:
 * @self: an #LrgRenderer
 *
 * Get the window this renderer is rendering to.
 *
 * Returns: (transfer none): the #LrgWindow
 */
LRG_AVAILABLE_IN_ALL
LrgWindow * lrg_renderer_get_window (LrgRenderer *self);

/* ==========================================================================
 * Drawable Rendering
 * ========================================================================== */

/**
 * lrg_renderer_render_drawable:
 * @self: an #LrgRenderer
 * @drawable: (transfer none): the drawable to render
 * @delta: the time delta since last frame
 *
 * Convenience method to render a drawable with the current camera.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_render_drawable (LrgRenderer *self,
                                   LrgDrawable *drawable,
                                   gfloat       delta);

/* ==========================================================================
 * Background Color
 * ========================================================================== */

/**
 * lrg_renderer_set_background_color:
 * @self: an #LrgRenderer
 * @color: the background color
 *
 * Set the default background color used when clearing the screen.
 */
LRG_AVAILABLE_IN_ALL
void lrg_renderer_set_background_color (LrgRenderer *self,
                                        GrlColor    *color);

/**
 * lrg_renderer_get_background_color:
 * @self: an #LrgRenderer
 *
 * Get the default background color.
 *
 * Returns: (transfer full): the background #GrlColor
 */
LRG_AVAILABLE_IN_ALL
GrlColor * lrg_renderer_get_background_color (LrgRenderer *self);

G_END_DECLS
