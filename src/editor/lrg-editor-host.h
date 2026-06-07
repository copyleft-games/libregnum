/* lrg-editor-host.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Embedding contract for the editor runtime.
 *
 * LrgEditorHost is to the editor what #LrgGameHost is to a hosted game: the
 * environment the editor borrows (engine, render target / FBO bracket, frame
 * timing, injected input), plus editor-specific concerns (the viewport camera,
 * 2D/3D mode, and an on-demand redraw request). A host IDE such as cmacs
 * implements this to drive the editor into an Emacs-buffer viewport; the
 * standalone engine-drawn editor uses the same interface against a window.
 *
 * LrgEditorSoftwareHost is a simple concrete host that stores the borrowed
 * environment — suitable for IDE-driven embedding and for tests.
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

/* ==========================================================================
 * LrgEditorHost interface
 * ========================================================================== */

#define LRG_TYPE_EDITOR_HOST (lrg_editor_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_INTERFACE (LrgEditorHost, lrg_editor_host, LRG, EDITOR_HOST, GObject)

/**
 * LrgEditorHostInterface:
 * @parent_iface: the parent interface
 * @get_engine: return the engine the editor borrows
 * @get_render_size: return the render-target pixel size
 * @begin_frame: begin the render-target bracket (bind FBO / backbuffer)
 * @clear: clear the render target to a colour
 * @end_frame: end the render-target bracket (flush)
 * @get_frame_delta: return seconds since the previous frame
 * @get_input_source: return the injected software input source, or %NULL
 * @get_viewport_camera: return the camera the editor renders through
 * @is_2d_mode: whether the viewport is in 2D editing mode
 * @request_redraw: ask the host to schedule another frame (event-driven hosts)
 *
 * The embedding contract for #LrgEditor.
 */
struct _LrgEditorHostInterface
{
	GTypeInterface parent_iface;

	LrgEngine *       (*get_engine)          (LrgEditorHost *self);
	void              (*get_render_size)     (LrgEditorHost *self,
	                                          gint          *width,
	                                          gint          *height);
	void              (*begin_frame)         (LrgEditorHost *self);
	void              (*clear)               (LrgEditorHost *self,
	                                          GrlColor      *color);
	void              (*end_frame)           (LrgEditorHost *self);
	gdouble           (*get_frame_delta)     (LrgEditorHost *self);
	LrgInputSoftware *(*get_input_source)    (LrgEditorHost *self);
	LrgCamera *       (*get_viewport_camera) (LrgEditorHost *self);
	gboolean          (*is_2d_mode)          (LrgEditorHost *self);
	void              (*request_redraw)      (LrgEditorHost *self);

	/*< private >*/
	gpointer _reserved[7];
};

LRG_AVAILABLE_IN_ALL
LrgEngine * lrg_editor_host_get_engine (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
void lrg_editor_host_get_render_size (LrgEditorHost *self, gint *width, gint *height);
LRG_AVAILABLE_IN_ALL
void lrg_editor_host_begin_frame (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
void lrg_editor_host_clear (LrgEditorHost *self, GrlColor *color);
LRG_AVAILABLE_IN_ALL
void lrg_editor_host_end_frame (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
gdouble lrg_editor_host_get_frame_delta (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
LrgInputSoftware * lrg_editor_host_get_input_source (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
LrgCamera * lrg_editor_host_get_viewport_camera (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
gboolean lrg_editor_host_is_2d_mode (LrgEditorHost *self);
LRG_AVAILABLE_IN_ALL
void lrg_editor_host_request_redraw (LrgEditorHost *self);

/* ==========================================================================
 * LrgEditorSoftwareHost — a concrete data-holding host
 * ========================================================================== */

#define LRG_TYPE_EDITOR_SOFTWARE_HOST (lrg_editor_software_host_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgEditorSoftwareHost, lrg_editor_software_host, LRG, EDITOR_SOFTWARE_HOST, GObject)

/**
 * lrg_editor_software_host_new:
 * @engine: (nullable) (transfer none): the engine to borrow
 *
 * Returns: (transfer full): a new #LrgEditorSoftwareHost
 */
LRG_AVAILABLE_IN_ALL
LrgEditorSoftwareHost * lrg_editor_software_host_new (LrgEngine *engine);

/**
 * lrg_editor_software_host_set_render_size:
 * @self: an #LrgEditorSoftwareHost
 * @width: render width in pixels
 * @height: render height in pixels
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_software_host_set_render_size (LrgEditorSoftwareHost *self,
                                               gint                   width,
                                               gint                   height);

/**
 * lrg_editor_software_host_set_frame_delta:
 * @self: an #LrgEditorSoftwareHost
 * @delta: seconds since the previous frame
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_software_host_set_frame_delta (LrgEditorSoftwareHost *self,
                                               gdouble                delta);

/**
 * lrg_editor_software_host_set_viewport_camera:
 * @self: an #LrgEditorSoftwareHost
 * @camera: (nullable) (transfer none): the viewport camera
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_software_host_set_viewport_camera (LrgEditorSoftwareHost *self,
                                                   LrgCamera             *camera);

/**
 * lrg_editor_software_host_set_input_source:
 * @self: an #LrgEditorSoftwareHost
 * @input: (nullable) (transfer none): the injected input source
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_software_host_set_input_source (LrgEditorSoftwareHost *self,
                                                LrgInputSoftware      *input);

/**
 * lrg_editor_software_host_set_2d_mode:
 * @self: an #LrgEditorSoftwareHost
 * @is_2d: whether the viewport is in 2D mode
 */
LRG_AVAILABLE_IN_ALL
void lrg_editor_software_host_set_2d_mode (LrgEditorSoftwareHost *self,
                                           gboolean               is_2d);

/**
 * lrg_editor_software_host_get_redraw_count:
 * @self: an #LrgEditorSoftwareHost
 *
 * Returns: how many times lrg_editor_host_request_redraw() has been called
 */
LRG_AVAILABLE_IN_ALL
guint lrg_editor_software_host_get_redraw_count (LrgEditorSoftwareHost *self);

G_END_DECLS
