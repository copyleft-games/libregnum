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

G_END_DECLS
