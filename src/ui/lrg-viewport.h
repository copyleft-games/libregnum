/* lrg-viewport.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgViewport: an area of the UI layout showing content rendered by
 * the application (a 3D scene, a 2D game, a video frame), with pointer,
 * key and touch input mapped into the content's pixel space.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * Enumerations
 * ========================================================================== */

/**
 * LrgViewportMode:
 * @LRG_VIEWPORT_MODE_RENDER: the viewport owns a render texture sized to
 *   its allocation (times #LrgViewport:resolution-scale, or a fixed
 *   resolution) and asks the application to draw into it every frame
 *   through #LrgViewport::render
 * @LRG_VIEWPORT_MODE_TEXTURE: the viewport shows a texture supplied by
 *   the application (lrg_viewport_set_texture(),
 *   lrg_viewport_set_source_render_texture())
 *
 * Where the viewport's content comes from.
 */
typedef enum
{
	LRG_VIEWPORT_MODE_RENDER,
	LRG_VIEWPORT_MODE_TEXTURE
} LrgViewportMode;

#define LRG_TYPE_VIEWPORT_MODE (lrg_viewport_mode_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_viewport_mode_get_type (void) G_GNUC_CONST;

/**
 * LrgViewportFit:
 * @LRG_VIEWPORT_FIT_STRETCH: fill the content area, ignoring the aspect ratio
 * @LRG_VIEWPORT_FIT_CONTAIN: the largest size that fits, keeping the aspect
 *   ratio; the remaining bars show the BOX background (letterbox)
 * @LRG_VIEWPORT_FIT_COVER: the smallest size that covers the area, keeping
 *   the aspect ratio; the overflow is cropped evenly on both sides
 * @LRG_VIEWPORT_FIT_INTEGER_SCALE: the largest whole-number scale that fits
 *   (at least 1), centred, for crisp pixel art; a texture larger than the
 *   area at scale 1 is cropped around its centre
 *
 * How the content's texture is scaled into the viewport.
 */
typedef enum
{
	LRG_VIEWPORT_FIT_STRETCH,
	LRG_VIEWPORT_FIT_CONTAIN,
	LRG_VIEWPORT_FIT_COVER,
	LRG_VIEWPORT_FIT_INTEGER_SCALE
} LrgViewportFit;

#define LRG_TYPE_VIEWPORT_FIT (lrg_viewport_fit_get_type ())

LRG_AVAILABLE_IN_ALL
GType lrg_viewport_fit_get_type (void) G_GNUC_CONST;

/* ==========================================================================
 * LrgViewport
 * ========================================================================== */

#define LRG_TYPE_VIEWPORT (lrg_viewport_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgViewport, lrg_viewport, LRG, VIEWPORT, LrgWidget)

/**
 * LrgViewportRenderFunc:
 * @viewport: the viewport
 * @target: the render texture being drawn into (texture mode is active)
 * @width: width of @target in pixels
 * @height: height of @target in pixels
 * @user_data: data passed to lrg_viewport_set_render_func()
 *
 * Draws the viewport's content. Called between BeginTextureMode() and
 * EndTextureMode() on @target, after the optional clear and before
 * #LrgViewport::render is emitted.
 */
typedef void (*LrgViewportRenderFunc) (LrgViewport      *viewport,
                                       GrlRenderTexture *target,
                                       gfloat            width,
                                       gfloat            height,
                                       gpointer          user_data);

/**
 * lrg_viewport_new:
 *
 * Creates a viewport in %LRG_VIEWPORT_MODE_RENDER that follows its
 * allocation at resolution scale 1, renders every frame and stretches
 * its texture over the content area.
 *
 * Returns: (transfer full): a new #LrgViewport
 */
LRG_AVAILABLE_IN_ALL
LrgViewport * lrg_viewport_new (void);

/* --------------------------------------------------------------------------
 * Mode and content
 * -------------------------------------------------------------------------- */

/**
 * lrg_viewport_get_mode:
 * @self: an #LrgViewport
 *
 * Returns: where the content comes from
 */
LRG_AVAILABLE_IN_ALL
LrgViewportMode lrg_viewport_get_mode (LrgViewport *self);

/**
 * lrg_viewport_set_mode:
 * @self: an #LrgViewport
 * @mode: the new mode
 *
 * Switches between rendering into an owned texture and showing an
 * application texture. Leaving %LRG_VIEWPORT_MODE_RENDER releases the
 * owned render texture.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_mode (LrgViewport     *self,
                            LrgViewportMode  mode);

/**
 * lrg_viewport_get_texture:
 * @self: an #LrgViewport
 *
 * Gets the application texture shown in %LRG_VIEWPORT_MODE_TEXTURE.
 *
 * Returns: (transfer none) (nullable): the texture
 */
LRG_AVAILABLE_IN_ALL
GrlTexture * lrg_viewport_get_texture (LrgViewport *self);

/**
 * lrg_viewport_set_texture:
 * @self: an #LrgViewport
 * @texture: (nullable): texture to show
 *
 * Shows @texture and switches to %LRG_VIEWPORT_MODE_TEXTURE. Resets
 * #LrgViewport:flip-y to %FALSE. The viewport keeps a reference.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_texture (LrgViewport *self,
                               GrlTexture  *texture);

/**
 * lrg_viewport_set_source_render_texture:
 * @self: an #LrgViewport
 * @target: (nullable): an application render texture to show
 *
 * Shows the colour buffer of @target (which the application renders
 * itself) and switches to %LRG_VIEWPORT_MODE_TEXTURE. Render textures
 * are stored bottom-up, so this sets #LrgViewport:flip-y. The viewport
 * keeps a reference to @target.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_source_render_texture (LrgViewport      *self,
                                             GrlRenderTexture *target);

/**
 * lrg_viewport_get_flip_y:
 * @self: an #LrgViewport
 *
 * Returns: whether the shown texture is flipped vertically
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_flip_y (LrgViewport *self);

/**
 * lrg_viewport_set_flip_y:
 * @self: an #LrgViewport
 * @flip_y: %TRUE to show the texture upside down (bottom-up storage)
 *
 * Only applies to %LRG_VIEWPORT_MODE_TEXTURE; the owned render texture
 * is always shown the right way up.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_flip_y (LrgViewport *self,
                              gboolean     flip_y);

/**
 * lrg_viewport_get_render_texture:
 * @self: an #LrgViewport
 *
 * Gets the render texture the viewport owns in
 * %LRG_VIEWPORT_MODE_RENDER. It is replaced when the viewport is
 * resized (see #LrgViewport::resized), so do not keep it.
 *
 * Returns: (transfer none) (nullable): the render texture, %NULL before
 *   the first frame or without a graphics context
 */
LRG_AVAILABLE_IN_ALL
GrlRenderTexture * lrg_viewport_get_render_texture (LrgViewport *self);

/* --------------------------------------------------------------------------
 * Rendering
 * -------------------------------------------------------------------------- */

/**
 * lrg_viewport_set_render_func:
 * @self: an #LrgViewport
 * @func: (nullable) (scope notified) (closure user_data) (destroy destroy): function drawing the content
 * @user_data: data for @func
 * @destroy: (nullable): frees @user_data when replaced or the viewport is finalized
 *
 * Sets a plain C callback drawing the content; it runs before the
 * #LrgViewport::render signal. Faster than a signal handler and
 * convenient from C.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_render_func (LrgViewport           *self,
                                   LrgViewportRenderFunc  func,
                                   gpointer               user_data,
                                   GDestroyNotify         destroy);

/**
 * lrg_viewport_render:
 * @self: an #LrgViewport
 *
 * Renders the content now: (re)creates the render texture if the
 * allocation changed, enters texture mode, clears it if
 * #LrgViewport:auto-clear is set, runs the render function and emits
 * #LrgViewport::render. lrg_canvas_update() calls this through the
 * widget's step every frame (#LrgViewport:continuous) or after
 * lrg_viewport_queue_render(); call it yourself only when the viewport
 * is not on a stepped canvas. Must not be called while another render
 * texture or the UI paint pass is active.
 *
 * Returns: %TRUE if the content was rendered, %FALSE in
 *   %LRG_VIEWPORT_MODE_TEXTURE, without a graphics context, with an
 *   empty allocation, or while another render texture is active
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_render (LrgViewport *self);

/**
 * lrg_viewport_queue_render:
 * @self: an #LrgViewport
 *
 * Asks for one render on the next step. Use it with
 * #LrgViewport:continuous off, when the scene changed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_queue_render (LrgViewport *self);

/**
 * lrg_viewport_get_continuous:
 * @self: an #LrgViewport
 *
 * Returns: whether the content is rendered every frame
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_continuous (LrgViewport *self);

/**
 * lrg_viewport_set_continuous:
 * @self: an #LrgViewport
 * @continuous: %TRUE to render every frame, %FALSE to render only after
 *   lrg_viewport_queue_render() (and after a resize)
 *
 * A continuous viewport reports itself busy from step, so
 * lrg_canvas_update() keeps returning %TRUE while it is visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_continuous (LrgViewport *self,
                                  gboolean     continuous);

/**
 * lrg_viewport_get_auto_clear:
 * @self: an #LrgViewport
 *
 * Returns: whether the render texture is cleared before each render
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_auto_clear (LrgViewport *self);

/**
 * lrg_viewport_set_auto_clear:
 * @self: an #LrgViewport
 * @auto_clear: %TRUE to clear to #LrgViewport:clear-color before rendering
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_auto_clear (LrgViewport *self,
                                  gboolean     auto_clear);

/**
 * lrg_viewport_get_clear_color:
 * @self: an #LrgViewport
 * @color: (out caller-allocates): the clear colour
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_get_clear_color (LrgViewport *self,
                                   GrlColor    *color);

/**
 * lrg_viewport_set_clear_color:
 * @self: an #LrgViewport
 * @color: the colour the render texture is cleared to
 *
 * Defaults to opaque black.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_clear_color (LrgViewport    *self,
                                   const GrlColor *color);

/* --------------------------------------------------------------------------
 * Resolution
 * -------------------------------------------------------------------------- */

/**
 * lrg_viewport_get_resolution_scale:
 * @self: an #LrgViewport
 *
 * Returns: render texture pixels per canvas unit
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_viewport_get_resolution_scale (LrgViewport *self);

/**
 * lrg_viewport_set_resolution_scale:
 * @self: an #LrgViewport
 * @scale: render texture pixels per canvas unit (0.1 .. 8); below 1
 *   renders cheaper and upscales, above 1 supersamples
 *
 * Ignored while a fixed resolution is set.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_resolution_scale (LrgViewport *self,
                                        gfloat       scale);

/**
 * lrg_viewport_get_fixed_resolution:
 * @self: an #LrgViewport
 * @width: (out) (optional): fixed width in pixels, 0 when following the allocation
 * @height: (out) (optional): fixed height in pixels, 0 when following the allocation
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_get_fixed_resolution (LrgViewport *self,
                                        gint        *width,
                                        gint        *height);

/**
 * lrg_viewport_set_fixed_resolution:
 * @self: an #LrgViewport
 * @width: render texture width in pixels, or 0
 * @height: render texture height in pixels, or 0
 *
 * Renders at exactly @width x @height whatever the allocation (pixel
 * art: 320 x 180 with %LRG_VIEWPORT_FIT_INTEGER_SCALE and a point
 * filter). 0 x 0 follows the allocation again. Both must be set for the
 * fixed resolution to apply.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_fixed_resolution (LrgViewport *self,
                                        gint         width,
                                        gint         height);

/**
 * lrg_viewport_get_resize_delay:
 * @self: an #LrgViewport
 *
 * Returns: seconds the allocation must stay unchanged before the
 *   render texture is recreated
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_viewport_get_resize_delay (LrgViewport *self);

/**
 * lrg_viewport_set_resize_delay:
 * @self: an #LrgViewport
 * @delay: seconds of canvas time (0 recreates on the next step)
 *
 * While a splitter or window is being dragged the allocation changes
 * every frame; recreating a GPU texture each time is wasteful, so the
 * old texture is stretched over the new size until the size has
 * settled for @delay seconds. The first texture is always created at
 * once. Default 0.1.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_resize_delay (LrgViewport *self,
                                    gfloat       delay);

/**
 * lrg_viewport_get_content_size:
 * @self: an #LrgViewport
 * @width: (out) (optional): width in texture pixels
 * @height: (out) (optional): height in texture pixels
 *
 * Gets the size of the content in pixels: the shown texture's size,
 * or in %LRG_VIEWPORT_MODE_RENDER without a texture yet (headless, or
 * before the first frame) the size it will be created at. Pointer
 * coordinates are reported in this space.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_get_content_size (LrgViewport *self,
                                    gint        *width,
                                    gint        *height);

/* --------------------------------------------------------------------------
 * Presentation
 * -------------------------------------------------------------------------- */

/**
 * lrg_viewport_get_fit:
 * @self: an #LrgViewport
 *
 * Returns: how the texture is scaled into the content area
 */
LRG_AVAILABLE_IN_ALL
LrgViewportFit lrg_viewport_get_fit (LrgViewport *self);

/**
 * lrg_viewport_set_fit:
 * @self: an #LrgViewport
 * @fit: the scaling rule
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_fit (LrgViewport    *self,
                           LrgViewportFit  fit);

/**
 * lrg_viewport_get_filter:
 * @self: an #LrgViewport
 *
 * Returns: the sampling filter of the owned render texture
 */
LRG_AVAILABLE_IN_ALL
GrlTextureFilter lrg_viewport_get_filter (LrgViewport *self);

/**
 * lrg_viewport_set_filter:
 * @self: an #LrgViewport
 * @filter: %GRL_TEXTURE_FILTER_POINT for pixel art,
 *   %GRL_TEXTURE_FILTER_BILINEAR (default) for smooth scaling
 *
 * Applied to the owned render texture. Application textures keep the
 * filter the application gave them.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_filter (LrgViewport      *self,
                              GrlTextureFilter  filter);

/**
 * lrg_viewport_compute_fit:
 * @fit: the scaling rule
 * @texture_width: content width in pixels
 * @texture_height: content height in pixels
 * @area: the rectangle to fit into
 * @dest: (out caller-allocates): where the visible part is drawn,
 *   always inside @area
 * @source: (out caller-allocates): the visible region of the texture,
 *   in pixels (smaller than the texture when cropped)
 *
 * The geometry used for painting and for mapping pointer positions,
 * exposed so applications can align overlays with the content.
 *
 * Returns: %FALSE when nothing is visible (empty texture or area)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_compute_fit (LrgViewportFit      fit,
                                   gfloat              texture_width,
                                   gfloat              texture_height,
                                   const GrlRectangle *area,
                                   GrlRectangle       *dest,
                                   GrlRectangle       *source);

/**
 * lrg_viewport_get_content_rect:
 * @self: an #LrgViewport
 * @rect: (out caller-allocates): the area inside the BOX border,
 *   relative to the widget
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_get_content_rect (LrgViewport  *self,
                                    GrlRectangle *rect);

/**
 * lrg_viewport_get_display_rect:
 * @self: an #LrgViewport
 * @rect: (out caller-allocates): where the content is drawn, relative
 *   to the widget (letterbox bars excluded)
 *
 * Returns: %FALSE when no content is visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_display_rect (LrgViewport  *self,
                                        GrlRectangle *rect);

/* --------------------------------------------------------------------------
 * Input
 * -------------------------------------------------------------------------- */

/**
 * lrg_viewport_widget_to_texture:
 * @self: an #LrgViewport
 * @x: X relative to the widget's left edge
 * @y: Y relative to the widget's top edge
 * @u: (out) (optional): X in content pixels (0 at the left)
 * @v: (out) (optional): Y in content pixels (0 at the top, as drawn)
 *
 * Maps a widget position to content pixels through the fit geometry.
 * @u and @v are set even outside the content (extrapolated), so drags
 * that leave the viewport keep reporting positions.
 *
 * Returns: %TRUE if the position is over the content, %FALSE over a
 *   letterbox bar, the border, or outside the widget
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_widget_to_texture (LrgViewport *self,
                                         gfloat       x,
                                         gfloat       y,
                                         gfloat      *u,
                                         gfloat      *v);

/**
 * lrg_viewport_texture_to_widget:
 * @self: an #LrgViewport
 * @u: X in content pixels
 * @v: Y in content pixels
 * @x: (out) (optional): X relative to the widget
 * @y: (out) (optional): Y relative to the widget
 *
 * The inverse of lrg_viewport_widget_to_texture(): places UI over a
 * point of the content (labels over projected 3D positions).
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_texture_to_widget (LrgViewport *self,
                                     gfloat       u,
                                     gfloat       v,
                                     gfloat      *x,
                                     gfloat      *y);

/**
 * lrg_viewport_get_lock_cursor:
 * @self: an #LrgViewport
 *
 * Returns: whether the OS cursor is hidden and locked during drags
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_lock_cursor (LrgViewport *self);

/**
 * lrg_viewport_set_lock_cursor:
 * @self: an #LrgViewport
 * @lock_cursor: %TRUE to hide and lock the cursor while a button
 *   pressed on the content is held (orbit and FPS-style cameras)
 *
 * While locked the pointer can move without bound, so
 * #LrgViewport::pointer-move keeps delivering deltas at the screen
 * edge. The cursor is released on button release and when the
 * viewport is disposed.
 */
LRG_AVAILABLE_IN_ALL
void lrg_viewport_set_lock_cursor (LrgViewport *self,
                                   gboolean     lock_cursor);

/**
 * lrg_viewport_get_dragging:
 * @self: an #LrgViewport
 *
 * Returns: %TRUE while a pointer button pressed on the content is held
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_viewport_get_dragging (LrgViewport *self);

G_END_DECLS
