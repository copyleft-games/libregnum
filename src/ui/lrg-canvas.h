/* lrg-canvas.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Root UI container that handles rendering and input dispatch.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"
#include "lrg-ui-event.h"
#include "lrg-canvas-input.h"

G_BEGIN_DECLS

#define LRG_TYPE_CANVAS (lrg_canvas_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCanvas, lrg_canvas, LRG, CANVAS, LrgContainer)

/**
 * lrg_canvas_new:
 *
 * Creates a new canvas - the root container for UI widgets.
 * The canvas handles rendering all child widgets and dispatching
 * input events to the appropriate widgets.
 *
 * Returns: (transfer full): A new #LrgCanvas
 */
LRG_AVAILABLE_IN_ALL
LrgCanvas * lrg_canvas_new (void);

/**
 * lrg_canvas_render:
 * @self: an #LrgCanvas
 *
 * Renders the entire widget tree starting from this canvas.
 * Should be called once per frame during the draw phase.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_render (LrgCanvas *self);

/**
 * lrg_canvas_update:
 * @self: an #LrgCanvas
 * @delta_time: seconds since the last frame
 *
 * Advances every widget's springs and appear animations.
 * lrg_canvas_process_input() calls this with the frame's delta time,
 * so call it yourself only when you skip input processing.
 *
 * Returns: %TRUE while something is animating
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_update (LrgCanvas *self,
                            gfloat     delta_time);

/**
 * lrg_canvas_get_paint_list:
 * @self: an #LrgCanvas
 *
 * The paint list the last lrg_canvas_render() recorded, for
 * inspection, profiling and tests.
 *
 * Returns: (transfer none): the paint list
 */
LRG_AVAILABLE_IN_ALL
LrgPaintList * lrg_canvas_get_paint_list (LrgCanvas *self);

/**
 * lrg_canvas_record:
 * @self: an #LrgCanvas
 *
 * Lays out the tree and records it into the canvas's paint list
 * without drawing it. lrg_canvas_render() is record + draw; headless
 * code and tests can record alone.
 *
 * Returns: (transfer none): the recorded paint list
 */
LRG_AVAILABLE_IN_ALL
LrgPaintList * lrg_canvas_record (LrgCanvas *self);

/**
 * lrg_canvas_get_paint_background:
 * @self: an #LrgCanvas
 *
 * Returns: %TRUE if the canvas fills itself with the theme's window colour
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_get_paint_background (LrgCanvas *self);

/**
 * lrg_canvas_set_paint_background:
 * @self: an #LrgCanvas
 * @paint_background: fill with %LRG_THEME_COLOR_WINDOW before painting
 *   (off by default so game scenes show through)
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_set_paint_background (LrgCanvas *self,
                                      gboolean   paint_background);

/**
 * lrg_canvas_handle_input:
 * @self: an #LrgCanvas
 *
 * Processes input and dispatches events to widgets.
 * Should be called once per frame, typically before rendering.
 *
 * This function polls the input state from graylib and creates
 * appropriate UI events (mouse move, button press/release, key events)
 * which are then dispatched to widgets in the tree.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_handle_input (LrgCanvas *self);

/**
 * lrg_canvas_process_input:
 * @self: an #LrgCanvas
 * @input: one frame of input, in canvas coordinates
 *
 * Turns @input into UI events and dispatches them. This is what
 * lrg_canvas_handle_input() calls after polling graylib; call it
 * directly to drive the canvas from your own input layer, a replay or
 * a test. @input->pointer_x/y must already be in canvas coordinates
 * (no pointer transform is applied here).
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_process_input (LrgCanvas            *self,
                               const LrgCanvasInput *input);

/**
 * LrgCanvasPointerTransform:
 * @x: (inout): pointer X coordinate to transform
 * @y: (inout): pointer Y coordinate to transform
 * @user_data: user data supplied at registration
 *
 * Maps raw window pointer coordinates into the coordinate space that
 * canvases render in. Needed when UI is drawn into a scaled render
 * target (e.g. a virtual-resolution game): the mouse reports window
 * pixels, but widgets are laid out in target coordinates.
 *
 * Since: 1.0
 */
typedef void (*LrgCanvasPointerTransform) (gfloat   *x,
                                           gfloat   *y,
                                           gpointer  user_data);

/**
 * lrg_canvas_set_default_pointer_transform:
 * @transform: (nullable) (scope notified): transform applied to pointer
 *   coordinates before hit-testing, or %NULL to restore raw coordinates
 * @user_data: user data passed to @transform
 *
 * Installs a process-wide pointer transform used by every canvas in
 * lrg_canvas_handle_input(). Whoever owns the scaled presentation
 * (e.g. #LrgGame2DTemplate's virtual-resolution pipeline) registers the
 * window-to-target mapping here so widget hit-testing matches what is
 * on screen.
 *
 * Since: 1.0
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_set_default_pointer_transform (LrgCanvasPointerTransform transform,
                                               gpointer                  user_data);

/**
 * lrg_canvas_widget_at_point:
 * @self: an #LrgCanvas
 * @x: the x coordinate
 * @y: the y coordinate
 *
 * Finds the topmost visible widget at the given screen coordinates.
 * The search is depth-first, returning the deepest widget that
 * contains the point.
 *
 * Returns: (transfer none) (nullable): The widget at the point, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_canvas_widget_at_point (LrgCanvas *self,
                                        gfloat     x,
                                        gfloat     y);

/**
 * lrg_canvas_get_focused_widget:
 * @self: an #LrgCanvas
 *
 * Gets the currently focused widget.
 *
 * Returns: (transfer none) (nullable): The focused widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_canvas_get_focused_widget (LrgCanvas *self);

/**
 * lrg_canvas_set_focused_widget:
 * @self: an #LrgCanvas
 * @widget: (nullable): the widget to focus, or %NULL to clear focus
 *
 * Sets the focused widget. The previously focused widget will receive
 * a focus-out event and the new widget will receive a focus-in event.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_set_focused_widget (LrgCanvas *self,
                                    LrgWidget *widget);

/**
 * lrg_canvas_get_hovered_widget:
 * @self: an #LrgCanvas
 *
 * Gets the widget currently under the mouse cursor.
 *
 * Returns: (transfer none) (nullable): The hovered widget, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_canvas_get_hovered_widget (LrgCanvas *self);

/* ==========================================================================
 * Focus navigation
 * ========================================================================== */

/**
 * lrg_canvas_get_navigation_enabled:
 * @self: an #LrgCanvas
 *
 * Returns: %TRUE if arrow keys, Tab and the gamepad move focus
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_get_navigation_enabled (LrgCanvas *self);

/**
 * lrg_canvas_set_navigation_enabled:
 * @self: an #LrgCanvas
 * @enabled: whether arrow keys, Tab and the gamepad move focus
 *   (on by default; turn off when the game drives focus itself)
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_set_navigation_enabled (LrgCanvas *self,
                                        gboolean   enabled);

/**
 * lrg_canvas_move_focus:
 * @self: an #LrgCanvas
 * @direction: where to move
 *
 * Moves keyboard focus. %LRG_NAV_DIRECTION_NEXT and
 * %LRG_NAV_DIRECTION_PREVIOUS walk the tab order (depth-first, wrapping);
 * the four directions pick the nearest focusable widget that way,
 * preferring ones in line with the current focus. Focus stays inside
 * an open modal popup. The new focus shows its focus ring.
 *
 * Returns: %TRUE if focus moved
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_move_focus (LrgCanvas       *self,
                                LrgNavDirection  direction);

/**
 * lrg_canvas_activate_focus:
 * @self: an #LrgCanvas
 *
 * Sends %LRG_UI_EVENT_ACTIVATE to the focused widget (what Enter,
 * Space and gamepad A do) with a short press animation.
 *
 * Returns: %TRUE if a widget handled it
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_canvas_activate_focus (LrgCanvas *self);

/* ==========================================================================
 * Popups
 * ========================================================================== */

/**
 * lrg_canvas_show_popup:
 * @self: an #LrgCanvas
 * @popup: widget to show above everything else; the canvas keeps a
 *   reference until it is dismissed
 * @anchor: (nullable): widget to place @popup next to
 * @placement: where to place it; flips to the other side when it would
 *   leave the canvas
 * @modal: modal popups dim the canvas, trap focus and swallow input
 *   outside them; non-modal popups close when clicking elsewhere
 *
 * Opens a popup (menus, combo lists, dialogs, tooltips). A popup with
 * zero size is given its preferred size. Without an appear animation
 * it pops in. The anchor gets %LRG_WIDGET_STATE_OPEN while it is open.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_show_popup (LrgCanvas         *self,
                            LrgWidget         *popup,
                            LrgWidget         *anchor,
                            LrgPopupPlacement  placement,
                            gboolean           modal);

/**
 * lrg_canvas_dismiss_popup:
 * @self: an #LrgCanvas
 * @popup: an open popup
 *
 * Closes @popup (and any popups opened after it). It shrinks back
 * towards its anchor while fading out, then is released.
 * #LrgCanvas::popup-dismissed is emitted.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_dismiss_popup (LrgCanvas *self,
                               LrgWidget *popup);

/**
 * lrg_canvas_get_top_popup:
 * @self: an #LrgCanvas
 *
 * Returns: (transfer none) (nullable): the most recent open popup
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_canvas_get_top_popup (LrgCanvas *self);

/**
 * lrg_canvas_get_popup_count:
 * @self: an #LrgCanvas
 *
 * Returns: number of open popups (closing ones excluded)
 */
LRG_AVAILABLE_IN_ALL
guint lrg_canvas_get_popup_count (LrgCanvas *self);

/**
 * lrg_canvas_place_popup:
 * @canvas_width: canvas width
 * @canvas_height: canvas height
 * @anchor_x: anchor left (canvas coordinates)
 * @anchor_y: anchor top
 * @anchor_width: anchor width (0 for a point)
 * @anchor_height: anchor height
 * @popup_width: popup width
 * @popup_height: popup height
 * @placement: requested placement
 * @x: (out): popup left
 * @y: (out): popup top
 *
 * The placement maths lrg_canvas_show_popup() uses: place next to the
 * anchor with a small gap, flip to the opposite side when the popup
 * does not fit, then clamp inside the canvas.
 *
 * Returns: the placement actually used (after flipping)
 */
LRG_AVAILABLE_IN_ALL
LrgPopupPlacement lrg_canvas_place_popup (gfloat             canvas_width,
                                          gfloat             canvas_height,
                                          gfloat             anchor_x,
                                          gfloat             anchor_y,
                                          gfloat             anchor_width,
                                          gfloat             anchor_height,
                                          gfloat             popup_width,
                                          gfloat             popup_height,
                                          LrgPopupPlacement  placement,
                                          gfloat            *x,
                                          gfloat            *y);

/* ==========================================================================
 * Overlay layer
 * ========================================================================== */

/**
 * lrg_canvas_add_overlay:
 * @self: an #LrgCanvas
 * @widget: a widget without a parent; the canvas takes a reference
 *
 * Adds @widget to the overlay layer, which is drawn and hit-tested
 * above everything else, including popups and the veil of a modal
 * dialog. Use it for notifications (an #LrgToastQueue), HUD layers and
 * cursors that must stay visible and bright while a dialog is open.
 * Overlay widgets are not reached by focus navigation; make full-layer
 * widgets pointer-transparent so clicks reach what is underneath.
 * The widget is parented to the canvas (coordinates and theme) and
 * sized like any canvas child: give it a size, or let it size itself.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_add_overlay (LrgCanvas *self,
                             LrgWidget *widget);

/**
 * lrg_canvas_remove_overlay:
 * @self: an #LrgCanvas
 * @widget: a widget added with lrg_canvas_add_overlay()
 *
 * Removes @widget from the overlay layer and releases it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_canvas_remove_overlay (LrgCanvas *self,
                                LrgWidget *widget);

/**
 * lrg_canvas_get_n_overlays:
 * @self: an #LrgCanvas
 *
 * Returns: number of widgets in the overlay layer
 */
LRG_AVAILABLE_IN_ALL
guint lrg_canvas_get_n_overlays (LrgCanvas *self);

G_END_DECLS
