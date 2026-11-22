/* lrg-world-widget.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgWorldWidget: the base of world-space UI -- small screen-space
 * drawings pinned to a point in the world (speech bubbles, nameplates,
 * emotes).  A widget has an anchor in world coordinates, a pixel offset,
 * a fade in and out and an optional hold time after which it hides
 * itself.  It is drawn over a 2D view through an LrgCamera2D or over a
 * 3D view through an LrgCamera3D (projected with
 * lrg_camera3d_project_point()), or straight at a screen point.
 *
 * Layout, fading and hit testing run without a window, so they are
 * testable; drawing needs a GL context and does nothing without one.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include <graylib.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "../graphics/lrg-camera2d.h"
#include "../graphics/lrg-camera3d.h"

G_BEGIN_DECLS

#define LRG_TYPE_WORLD_WIDGET (lrg_world_widget_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWorldWidget, lrg_world_widget, LRG, WORLD_WIDGET, GObject)

/**
 * LrgWorldWidgetClass:
 * @parent_class: the parent class
 * @measure: computes the widget's size in pixels at its current scale
 * @draw_box: draws the widget with its box at (@x, @y) (top-left, pixels)
 *   and the anchor point at (@anchor_x, @anchor_y); @alpha is the fade
 *   (0 to 1).  Only called with a GL context.
 * @update: advances animations by @delta seconds; chain up so the fade
 *   and hold time keep working
 *
 * The class of world-space widgets.  A subclass implements @measure and
 * @draw_box; the base places the box centred above the anchor (its
 * bottom edge @offset pixels above it).
 *
 * Since: 0.11.0
 */
struct _LrgWorldWidgetClass
{
	GObjectClass parent_class;

	void (*measure)  (LrgWorldWidget *self,
	                  gfloat         *width,
	                  gfloat         *height);
	void (*draw_box) (LrgWorldWidget *self,
	                  gfloat          x,
	                  gfloat          y,
	                  gfloat          anchor_x,
	                  gfloat          anchor_y,
	                  gfloat          alpha);
	void (*update)   (LrgWorldWidget *self,
	                  gfloat          delta);

	/*< private >*/
	gpointer _reserved[8];
};

/**
 * lrg_world_widget_set_anchor:
 * @self: an #LrgWorldWidget
 * @x: world x
 * @y: world y
 * @z: world z (ignored by 2D cameras)
 *
 * Pins the widget to a point in the world.  Call it every frame for a
 * widget following something that moves.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_set_anchor (LrgWorldWidget *self,
                                  gfloat          x,
                                  gfloat          y,
                                  gfloat          z);

/**
 * lrg_world_widget_get_anchor:
 * @self: an #LrgWorldWidget
 * @x: (out) (optional): world x
 * @y: (out) (optional): world y
 * @z: (out) (optional): world z
 *
 * Gets the world point the widget is pinned to.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_get_anchor (LrgWorldWidget *self,
                                  gfloat         *x,
                                  gfloat         *y,
                                  gfloat         *z);

/**
 * lrg_world_widget_set_offset:
 * @self: an #LrgWorldWidget
 * @dx: horizontal shift of the box in pixels (positive is right)
 * @dy: gap in pixels between the anchor and the box's bottom edge
 *   (positive raises the box; negative puts it below the anchor)
 *
 * Moves the box relative to its anchor on screen.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_set_offset (LrgWorldWidget *self,
                                  gfloat          dx,
                                  gfloat          dy);

/**
 * lrg_world_widget_get_offset:
 * @self: an #LrgWorldWidget
 * @dx: (out) (optional): horizontal shift in pixels
 * @dy: (out) (optional): gap above the anchor in pixels
 *
 * Gets the screen offset set with lrg_world_widget_set_offset().
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_get_offset (LrgWorldWidget *self,
                                  gfloat         *dx,
                                  gfloat         *dy);

/**
 * lrg_world_widget_set_scale:
 * @self: an #LrgWorldWidget
 * @scale: size multiplier (> 0; 1 is the widget's own pixel sizes)
 *
 * Scales the whole widget, for example by distance from a 3D camera.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_set_scale (LrgWorldWidget *self,
                                 gfloat          scale);

/**
 * lrg_world_widget_get_scale:
 * @self: an #LrgWorldWidget
 *
 * Returns: the size multiplier
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_world_widget_get_scale (LrgWorldWidget *self);

/**
 * lrg_world_widget_show:
 * @self: an #LrgWorldWidget
 * @fade_seconds: fade-in time (0 shows at once)
 *
 * Shows the widget, fading in from its current alpha, and restarts the
 * hold timer.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_show (LrgWorldWidget *self,
                            gfloat          fade_seconds);

/**
 * lrg_world_widget_hide:
 * @self: an #LrgWorldWidget
 * @fade_seconds: fade-out time (0 hides at once)
 *
 * Fades the widget out.  #LrgWorldWidget::hidden is emitted when the
 * alpha reaches 0.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_hide (LrgWorldWidget *self,
                            gfloat          fade_seconds);

/**
 * lrg_world_widget_is_shown:
 * @self: an #LrgWorldWidget
 *
 * Returns: %TRUE while the widget is shown or fading in (a widget fading
 *   out is not shown)
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_widget_is_shown (LrgWorldWidget *self);

/**
 * lrg_world_widget_get_alpha:
 * @self: an #LrgWorldWidget
 *
 * Returns: the current fade, 0 (invisible) to 1
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_world_widget_get_alpha (LrgWorldWidget *self);

/**
 * lrg_world_widget_set_hold_time:
 * @self: an #LrgWorldWidget
 * @seconds: how long the widget stays fully shown before it fades out
 *   on its own; 0 keeps it until lrg_world_widget_hide()
 * @fade_out_seconds: the fade-out used when the hold time ends
 *
 * Makes the widget hide itself after a while (a speech bubble that
 * disappears after the line is read).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_set_hold_time (LrgWorldWidget *self,
                                     gfloat          seconds,
                                     gfloat          fade_out_seconds);

/**
 * lrg_world_widget_update:
 * @self: an #LrgWorldWidget
 * @delta: seconds since the last update
 *
 * Advances the fade, the hold timer and the subclass's animations.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_update (LrgWorldWidget *self,
                              gfloat          delta);

/**
 * lrg_world_widget_measure:
 * @self: an #LrgWorldWidget
 * @width: (out) (optional): width in pixels
 * @height: (out) (optional): height in pixels
 *
 * Gets the widget's size at its current scale.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_measure (LrgWorldWidget *self,
                               gfloat         *width,
                               gfloat         *height);

/**
 * lrg_world_widget_get_box_at:
 * @self: an #LrgWorldWidget
 * @anchor_x: the anchor's screen x
 * @anchor_y: the anchor's screen y
 * @out_box: (out caller-allocates): the box in screen pixels
 *
 * Where the widget's box goes when its anchor is at a screen point:
 * centred horizontally on the anchor (plus the x offset), its bottom
 * edge the y offset above it.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_get_box_at (LrgWorldWidget *self,
                                  gfloat          anchor_x,
                                  gfloat          anchor_y,
                                  GrlRectangle   *out_box);

/**
 * lrg_world_widget_project_2d:
 * @self: an #LrgWorldWidget
 * @camera: the 2D camera the world is drawn with
 * @out_screen: (out caller-allocates): the anchor on screen
 *
 * Projects the anchor through a 2D camera (its x and y).
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_project_2d (LrgWorldWidget *self,
                                  LrgCamera2D    *camera,
                                  GrlVector2     *out_screen);

/**
 * lrg_world_widget_project_3d:
 * @self: an #LrgWorldWidget
 * @camera: the 3D camera the world is drawn with
 * @viewport_width: width of the view in pixels
 * @viewport_height: height of the view in pixels
 * @out_screen: (out caller-allocates): the anchor on screen
 *
 * Projects the anchor through a 3D camera (lrg_camera3d_project_point()).
 *
 * Returns: %FALSE when the anchor is behind the camera
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_widget_project_3d (LrgWorldWidget *self,
                                      LrgCamera3D    *camera,
                                      gint            viewport_width,
                                      gint            viewport_height,
                                      GrlVector2     *out_screen);

/**
 * lrg_world_widget_draw_at:
 * @self: an #LrgWorldWidget
 * @anchor_x: the anchor's screen x
 * @anchor_y: the anchor's screen y
 *
 * Draws the widget with its anchor at a screen point, outside any
 * camera mode.  Records the box for lrg_world_widget_contains_point()
 * even without a GL context, when nothing is drawn.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_draw_at (LrgWorldWidget *self,
                               gfloat          anchor_x,
                               gfloat          anchor_y);

/**
 * lrg_world_widget_draw_2d:
 * @self: an #LrgWorldWidget
 * @camera: the 2D camera the world is drawn with
 *
 * Draws the widget over a 2D view.  Call it after lrg_camera_end(), in
 * screen space, so text stays sharp at any zoom.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_draw_2d (LrgWorldWidget *self,
                               LrgCamera2D    *camera);

/**
 * lrg_world_widget_draw_3d:
 * @self: an #LrgWorldWidget
 * @camera: the 3D camera the world is drawn with
 * @viewport_width: width of the view in pixels
 * @viewport_height: height of the view in pixels
 *
 * Draws the widget over a 3D view, after the 3D pass (outside the camera
 * mode).  Nothing is drawn when the anchor is behind the camera.
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_world_widget_draw_3d (LrgWorldWidget *self,
                               LrgCamera3D    *camera,
                               gint            viewport_width,
                               gint            viewport_height);

/**
 * lrg_world_widget_get_last_box:
 * @self: an #LrgWorldWidget
 * @out_box: (out caller-allocates): the box last drawn
 *
 * Returns: %FALSE when the widget has not been drawn since it was last
 *   hidden
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_widget_get_last_box (LrgWorldWidget *self,
                                        GrlRectangle   *out_box);

/**
 * lrg_world_widget_contains_point:
 * @self: an #LrgWorldWidget
 * @x: screen x (a mouse position)
 * @y: screen y
 *
 * Hit-tests the box last drawn.
 *
 * Returns: %TRUE when (@x, @y) is inside it
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_world_widget_contains_point (LrgWorldWidget *self,
                                          gfloat          x,
                                          gfloat          y);

/**
 * lrg_world_widget_measure_text:
 * @text: UTF-8 text
 * @font_size: font size in pixels
 *
 * Measures one line in raylib's default font.  With a GL context this
 * is MeasureText(); without one it is an estimate of 0.6 x @font_size
 * per character (the default font's average advance), so layouts can be
 * computed and tested headless.
 *
 * Returns: the width in pixels
 *
 * Since: 0.11.0
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_world_widget_measure_text (const gchar *text,
                                      gfloat       font_size);

G_END_DECLS
