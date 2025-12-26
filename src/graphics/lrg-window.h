/* lrg-window.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract window base class for different backends.
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

#define LRG_TYPE_WINDOW (lrg_window_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWindow, lrg_window, LRG, WINDOW, GObject)

/**
 * LrgWindowClass:
 * @parent_class: Parent class
 * @begin_frame: Begin a rendering frame
 * @end_frame: End a rendering frame
 * @should_close: Check if window should close
 * @set_should_close: Set whether window should close
 * @poll_input: Poll for input events
 * @get_frame_time: Get time since last frame
 * @get_fps: Get current frames per second
 * @clear: Clear the window with a color
 * @show: Show the window
 * @hide: Hide the window
 *
 * Abstract class for window implementations.
 *
 * Subclasses must implement all pure virtual methods to provide
 * a working window backend. The graylib backend is provided by
 * #LrgGrlWindow, and other backends (like GTK) can be added.
 */
struct _LrgWindowClass
{
	GObjectClass parent_class;

	/*< public >*/

	/* Pure virtual - must implement */
	void      (*begin_frame)      (LrgWindow *self);
	void      (*end_frame)        (LrgWindow *self);
	gboolean  (*should_close)     (LrgWindow *self);
	void      (*set_should_close) (LrgWindow *self,
	                               gboolean   close);
	void      (*poll_input)       (LrgWindow *self);
	gfloat    (*get_frame_time)   (LrgWindow *self);
	gint      (*get_fps)          (LrgWindow *self);
	void      (*clear)            (LrgWindow *self,
	                               GrlColor  *color);

	/* Virtual with default implementations */
	void      (*show)             (LrgWindow *self);
	void      (*hide)             (LrgWindow *self);

	/*< private >*/
	gpointer _reserved[8];
};

/* ==========================================================================
 * Frame Management
 * ========================================================================== */

/**
 * lrg_window_begin_frame:
 * @self: an #LrgWindow
 *
 * Begin a rendering frame.
 *
 * This must be called before any drawing operations.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_begin_frame (LrgWindow *self);

/**
 * lrg_window_end_frame:
 * @self: an #LrgWindow
 *
 * End a rendering frame.
 *
 * This must be called after all drawing operations are complete.
 * It presents the frame to the screen.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_end_frame (LrgWindow *self);

/**
 * lrg_window_clear:
 * @self: an #LrgWindow
 * @color: the color to clear with
 *
 * Clear the window background with the specified color.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_clear (LrgWindow *self,
                       GrlColor  *color);

/* ==========================================================================
 * Window State
 * ========================================================================== */

/**
 * lrg_window_should_close:
 * @self: an #LrgWindow
 *
 * Check if the window should close.
 *
 * Returns: %TRUE if the window should close
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_window_should_close (LrgWindow *self);

/**
 * lrg_window_set_should_close:
 * @self: an #LrgWindow
 * @close: whether the window should close
 *
 * Set whether the window should close.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_set_should_close (LrgWindow *self,
                                  gboolean   close);

/**
 * lrg_window_show:
 * @self: an #LrgWindow
 *
 * Show the window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_show (LrgWindow *self);

/**
 * lrg_window_hide:
 * @self: an #LrgWindow
 *
 * Hide the window.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_hide (LrgWindow *self);

/* ==========================================================================
 * Input
 * ========================================================================== */

/**
 * lrg_window_poll_input:
 * @self: an #LrgWindow
 *
 * Poll for input events.
 *
 * This processes pending input events and emits appropriate signals.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_poll_input (LrgWindow *self);

/* ==========================================================================
 * Timing
 * ========================================================================== */

/**
 * lrg_window_get_frame_time:
 * @self: an #LrgWindow
 *
 * Get the time since the last frame.
 *
 * Returns: time in seconds since last frame
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_window_get_frame_time (LrgWindow *self);

/**
 * lrg_window_get_fps:
 * @self: an #LrgWindow
 *
 * Get the current frames per second.
 *
 * Returns: current FPS
 */
LRG_AVAILABLE_IN_ALL
gint lrg_window_get_fps (LrgWindow *self);

/* ==========================================================================
 * Properties
 * ========================================================================== */

/**
 * lrg_window_get_title:
 * @self: an #LrgWindow
 *
 * Get the window title.
 *
 * Returns: (transfer none): the window title
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_window_get_title (LrgWindow *self);

/**
 * lrg_window_set_title:
 * @self: an #LrgWindow
 * @title: the new title
 *
 * Set the window title.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_set_title (LrgWindow   *self,
                           const gchar *title);

/**
 * lrg_window_get_width:
 * @self: an #LrgWindow
 *
 * Get the window width.
 *
 * Returns: the window width in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_window_get_width (LrgWindow *self);

/**
 * lrg_window_get_height:
 * @self: an #LrgWindow
 *
 * Get the window height.
 *
 * Returns: the window height in pixels
 */
LRG_AVAILABLE_IN_ALL
gint lrg_window_get_height (LrgWindow *self);

/**
 * lrg_window_get_target_fps:
 * @self: an #LrgWindow
 *
 * Get the target frames per second.
 *
 * Returns: the target FPS
 */
LRG_AVAILABLE_IN_ALL
gint lrg_window_get_target_fps (LrgWindow *self);

/**
 * lrg_window_set_target_fps:
 * @self: an #LrgWindow
 * @fps: the target FPS
 *
 * Set the target frames per second.
 */
LRG_AVAILABLE_IN_ALL
void lrg_window_set_target_fps (LrgWindow *self,
                                gint       fps);

G_END_DECLS
