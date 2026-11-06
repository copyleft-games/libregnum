/* lrg-header-bar.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgHeaderBar: the title bar of a window that draws its own decorations.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

/**
 * LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT:
 *
 * The default #LrgHeaderBar:decoration-layout: minimise, maximise and
 * close at the trailing edge.
 */
#define LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT ":minimize,maximize,close"

/**
 * LrgHeaderBarButton:
 * @LRG_HEADER_BAR_BUTTON_NONE: no button
 * @LRG_HEADER_BAR_BUTTON_MINIMIZE: minimise (iconify) the window
 * @LRG_HEADER_BAR_BUTTON_MAXIMIZE: maximise, or restore when maximised
 * @LRG_HEADER_BAR_BUTTON_CLOSE: close the window
 *
 * The window buttons a header bar can draw.
 */
typedef enum
{
	LRG_HEADER_BAR_BUTTON_NONE,
	LRG_HEADER_BAR_BUTTON_MINIMIZE,
	LRG_HEADER_BAR_BUTTON_MAXIMIZE,
	LRG_HEADER_BAR_BUTTON_CLOSE
} LrgHeaderBarButton;

LRG_AVAILABLE_IN_ALL
GType lrg_header_bar_button_get_type (void) G_GNUC_CONST;
#define LRG_TYPE_HEADER_BAR_BUTTON (lrg_header_bar_button_get_type ())

/**
 * LrgHeaderBarWindowFuncs:
 * @can_move: whether the window can be moved by the application (no
 *   window yet, or a Wayland session, returns %FALSE)
 * @get_position: reads the window position in screen pixels
 * @set_position: moves the window to a position in screen pixels
 * @minimize: iconifies the window
 * @is_maximized: whether the window is maximised
 * @maximize: maximises the window
 * @restore: restores a maximised window
 *
 * The window operations a header bar performs. The default set talks
 * to the raylib window (guarded by IsWindowReady()); tests and apps
 * that own their window in another way install their own with
 * lrg_header_bar_set_window_funcs(). Every member may be %NULL, which
 * makes that operation a no-op.
 */
typedef struct
{
	gboolean (*can_move)     (gpointer user_data);
	void     (*get_position) (gpointer user_data,
	                          gint    *x,
	                          gint    *y);
	void     (*set_position) (gpointer user_data,
	                          gint     x,
	                          gint     y);
	void     (*minimize)     (gpointer user_data);
	gboolean (*is_maximized) (gpointer user_data);
	void     (*maximize)     (gpointer user_data);
	void     (*restore)      (gpointer user_data);
} LrgHeaderBarWindowFuncs;

#define LRG_TYPE_HEADER_BAR (lrg_header_bar_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgHeaderBar, lrg_header_bar, LRG, HEADER_BAR, LrgContainer)

/**
 * lrg_header_bar_new:
 *
 * Creates a header bar with the default decoration layout and no title.
 *
 * Returns: (transfer full): a new #LrgHeaderBar
 */
LRG_AVAILABLE_IN_ALL
LrgHeaderBar * lrg_header_bar_new (void);

/**
 * lrg_header_bar_pack_start:
 * @self: an #LrgHeaderBar
 * @child: a widget
 *
 * Adds @child after the other start children, from the leading edge
 * (the left in left-to-right). lrg_container_add_child() does the same.
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_pack_start (LrgHeaderBar *self,
                                LrgWidget    *child);

/**
 * lrg_header_bar_pack_end:
 * @self: an #LrgHeaderBar
 * @child: a widget
 *
 * Adds @child after the other end children, from the trailing edge
 * inwards (the first end child sits next to the window buttons).
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_pack_end (LrgHeaderBar *self,
                              LrgWidget    *child);

/**
 * lrg_header_bar_get_child_packing:
 * @self: an #LrgHeaderBar
 * @child: a child of @self
 *
 * Returns: %TRUE if @child is packed at the end, %FALSE for the start
 *   (or the title widget)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_get_child_packing (LrgHeaderBar *self,
                                           LrgWidget    *child);

/**
 * lrg_header_bar_set_child_packing:
 * @self: an #LrgHeaderBar
 * @child: a child of @self
 * @pack_end: %TRUE to move @child to the end group
 *
 * Moves a child between the start and end groups (used by the loader
 * for a per-child "pack" key).
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_child_packing (LrgHeaderBar *self,
                                       LrgWidget    *child,
                                       gboolean      pack_end);

/**
 * lrg_header_bar_get_title:
 * @self: an #LrgHeaderBar
 *
 * Returns: (nullable): the title
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_header_bar_get_title (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_title:
 * @self: an #LrgHeaderBar
 * @title: (nullable): the title, centred on the bar
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_title (LrgHeaderBar *self,
                               const gchar  *title);

/**
 * lrg_header_bar_get_subtitle:
 * @self: an #LrgHeaderBar
 *
 * Returns: (nullable): the subtitle
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_header_bar_get_subtitle (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_subtitle:
 * @self: an #LrgHeaderBar
 * @subtitle: (nullable): a smaller, muted line under the title
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_subtitle (LrgHeaderBar *self,
                                  const gchar  *subtitle);

/**
 * lrg_header_bar_get_title_widget:
 * @self: an #LrgHeaderBar
 *
 * Returns: (transfer none) (nullable): the custom title widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_header_bar_get_title_widget (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_title_widget:
 * @self: an #LrgHeaderBar
 * @widget: (nullable): a widget centred in place of the title and
 *   subtitle (a search entry, a switcher), or %NULL to go back to the
 *   text title; the previous title widget is removed
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_title_widget (LrgHeaderBar *self,
                                      LrgWidget    *widget);

/**
 * lrg_header_bar_get_show_window_buttons:
 * @self: an #LrgHeaderBar
 *
 * Returns: %TRUE if the window buttons are drawn
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_get_show_window_buttons (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_show_window_buttons:
 * @self: an #LrgHeaderBar
 * @show: draw the buttons named by the decoration layout (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_show_window_buttons (LrgHeaderBar *self,
                                             gboolean      show);

/**
 * lrg_header_bar_get_decoration_layout:
 * @self: an #LrgHeaderBar
 *
 * Returns: the decoration layout string
 */
LRG_AVAILABLE_IN_ALL
const gchar * lrg_header_bar_get_decoration_layout (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_decoration_layout:
 * @self: an #LrgHeaderBar
 * @layout: (nullable): GTK-style layout: comma-separated button names
 *   before the colon go to the leading edge, after it to the trailing
 *   edge ("close:" for macOS style, "menu:minimize,maximize,close").
 *   Known names are "minimize", "maximize" and "close"; others (such as
 *   "menu" and "icon") are ignored. %NULL restores
 *   %LRG_HEADER_BAR_DEFAULT_DECORATION_LAYOUT.
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_decoration_layout (LrgHeaderBar *self,
                                           const gchar  *layout);

/**
 * lrg_header_bar_parse_decoration_layout:
 * @layout: (nullable): a decoration layout string
 * @start: (out caller-allocates) (array fixed-size=3): buttons at the
 *   leading edge, in order, padded with %LRG_HEADER_BAR_BUTTON_NONE
 * @end: (out caller-allocates) (array fixed-size=3): buttons at the
 *   trailing edge, in order, padded with %LRG_HEADER_BAR_BUTTON_NONE
 *
 * Parses a decoration layout the way the header bar does: whitespace
 * is ignored, unknown names are skipped and each button is used at
 * most once (the first occurrence wins). A layout without a colon puts
 * everything at the leading edge, like GTK.
 *
 * Returns: the total number of buttons found
 */
LRG_AVAILABLE_IN_ALL
guint lrg_header_bar_parse_decoration_layout (const gchar        *layout,
                                              LrgHeaderBarButton  start[3],
                                              LrgHeaderBarButton  end[3]);

/**
 * lrg_header_bar_get_draggable:
 * @self: an #LrgHeaderBar
 *
 * Returns: %TRUE if dragging the bar moves the window
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_get_draggable (LrgHeaderBar *self);

/**
 * lrg_header_bar_set_draggable:
 * @self: an #LrgHeaderBar
 * @draggable: let presses on empty bar space drag the window and
 *   double clicks toggle maximise (default %TRUE)
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_draggable (LrgHeaderBar *self,
                                   gboolean      draggable);

/**
 * lrg_header_bar_is_drag_area:
 * @self: an #LrgHeaderBar
 * @x: world X
 * @y: world Y
 *
 * Whether a press at the point would drag the window: it is inside the
 * bar, not on a window button and not on an interactive (focusable)
 * child. Labels and other inert children count as drag area. This does
 * not consider #LrgHeaderBar:draggable or whether the platform allows
 * moving windows.
 *
 * Returns: %TRUE for a drag area
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_is_drag_area (LrgHeaderBar *self,
                                      gfloat        x,
                                      gfloat        y);

/**
 * lrg_header_bar_get_button_at:
 * @self: an #LrgHeaderBar
 * @x: world X
 * @y: world Y
 *
 * Returns: the window button under the point, or
 *   %LRG_HEADER_BAR_BUTTON_NONE
 */
LRG_AVAILABLE_IN_ALL
LrgHeaderBarButton lrg_header_bar_get_button_at (LrgHeaderBar *self,
                                                 gfloat        x,
                                                 gfloat        y);

/**
 * lrg_header_bar_get_button_rect:
 * @self: an #LrgHeaderBar
 * @button: a window button
 * @x: (out) (optional): world left edge
 * @y: (out) (optional): world top edge
 * @width: (out) (optional): width
 * @height: (out) (optional): height
 *
 * Where @button is drawn, after mirroring for right-to-left.
 *
 * Returns: %TRUE if @button is shown
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_get_button_rect (LrgHeaderBar       *self,
                                         LrgHeaderBarButton  button,
                                         gfloat             *x,
                                         gfloat             *y,
                                         gfloat             *width,
                                         gfloat             *height);

/**
 * lrg_header_bar_activate_button:
 * @self: an #LrgHeaderBar
 * @button: the window button to act as if clicked
 *
 * Does what clicking @button does: close emits
 * #LrgHeaderBar::close-requested and, unless a handler returns %TRUE,
 * activates "app.quit" (lrg_widget_activate_action(), which ends in
 * #LrgCanvas::quit-requested); minimise and maximise call the window
 * functions.
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_activate_button (LrgHeaderBar       *self,
                                     LrgHeaderBarButton  button);

/**
 * lrg_header_bar_set_window_funcs:
 * @self: an #LrgHeaderBar
 * @funcs: (nullable): the window operations (copied), or %NULL for the
 *   default raylib ones
 * @user_data: (nullable): data passed to every function
 * @destroy: (nullable): frees @user_data when the functions are replaced
 *   or the bar is finalised
 *
 * Replaces the window operations the bar uses for dragging, double
 * click and the minimise / maximise buttons.
 */
LRG_AVAILABLE_IN_ALL
void lrg_header_bar_set_window_funcs (LrgHeaderBar                  *self,
                                      const LrgHeaderBarWindowFuncs *funcs,
                                      gpointer                       user_data,
                                      GDestroyNotify                 destroy);

/**
 * lrg_header_bar_get_default_window_funcs:
 *
 * The raylib window operations: GetWindowPosition() /
 * SetWindowPosition(), MinimizeWindow(), IsWindowMaximized(),
 * MaximizeWindow() and RestoreWindow(), each a no-op until
 * IsWindowReady(). can_move is %FALSE on Wayland, where clients cannot
 * position their own windows.
 *
 * Returns: (transfer none): the default functions (static)
 */
LRG_AVAILABLE_IN_ALL
const LrgHeaderBarWindowFuncs * lrg_header_bar_get_default_window_funcs (void);

/**
 * lrg_header_bar_get_can_move_window:
 * @self: an #LrgHeaderBar
 *
 * Returns: %TRUE if dragging would actually move the window (a window
 *   exists and the platform lets the application position it)
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_header_bar_get_can_move_window (LrgHeaderBar *self);

G_END_DECLS
