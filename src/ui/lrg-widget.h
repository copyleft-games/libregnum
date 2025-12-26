/* lrg-widget.h
 *
 * Copyright 2025 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Abstract base class for UI widgets.
 *
 * Widgets are the building blocks of the UI system. They can be
 * positioned, sized, drawn, and respond to user input events.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "../lrg-types.h"
#include "lrg-ui-event.h"

G_BEGIN_DECLS

#define LRG_TYPE_WIDGET (lrg_widget_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_DERIVABLE_TYPE (LrgWidget, lrg_widget, LRG, WIDGET, GObject)

/**
 * LrgWidgetClass:
 * @parent_class: The parent class
 * @draw: Virtual method to draw the widget
 * @measure: Virtual method to calculate preferred size
 * @handle_event: Virtual method to handle input events
 *
 * The class structure for #LrgWidget.
 *
 * Subclasses must override @draw to render themselves.
 * The @measure method should return the preferred size.
 * The @handle_event method should process input and return
 * %TRUE if the event was consumed.
 */
struct _LrgWidgetClass
{
    GObjectClass parent_class;

    /* Virtual methods */

    /**
     * LrgWidgetClass::draw:
     * @self: the widget
     *
     * Draws the widget. Subclasses must override this method
     * to render their visual content using graylib drawing functions.
     *
     * The widget should draw at its world position (use
     * lrg_widget_get_world_x() and lrg_widget_get_world_y()).
     */
    void     (*draw)         (LrgWidget *self);

    /**
     * LrgWidgetClass::measure:
     * @self: the widget
     * @preferred_width: (out): location to store preferred width
     * @preferred_height: (out): location to store preferred height
     *
     * Calculates the widget's preferred size. Containers use this
     * during layout to determine how much space children need.
     */
    void     (*measure)      (LrgWidget *self,
                              gfloat    *preferred_width,
                              gfloat    *preferred_height);

    /**
     * LrgWidgetClass::handle_event:
     * @self: the widget
     * @event: the UI event to handle
     *
     * Handles a UI event. The widget should process the event
     * and return %TRUE if it was consumed, %FALSE to let it
     * propagate to the parent.
     *
     * Returns: %TRUE if the event was consumed
     */
    gboolean (*handle_event) (LrgWidget        *self,
                              const LrgUIEvent *event);

    /*< private >*/
    gpointer _reserved[8];
};

/* ==========================================================================
 * Position and Size
 * ========================================================================== */

/**
 * lrg_widget_get_x:
 * @self: an #LrgWidget
 *
 * Gets the widget's X position relative to its parent.
 *
 * Returns: The X position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_x (LrgWidget *self);

/**
 * lrg_widget_set_x:
 * @self: an #LrgWidget
 * @x: the X position
 *
 * Sets the widget's X position relative to its parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_x (LrgWidget *self,
                       gfloat     x);

/**
 * lrg_widget_get_y:
 * @self: an #LrgWidget
 *
 * Gets the widget's Y position relative to its parent.
 *
 * Returns: The Y position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_y (LrgWidget *self);

/**
 * lrg_widget_set_y:
 * @self: an #LrgWidget
 * @y: the Y position
 *
 * Sets the widget's Y position relative to its parent.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_y (LrgWidget *self,
                       gfloat     y);

/**
 * lrg_widget_get_width:
 * @self: an #LrgWidget
 *
 * Gets the widget's width.
 *
 * Returns: The width
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_width (LrgWidget *self);

/**
 * lrg_widget_set_width:
 * @self: an #LrgWidget
 * @width: the width
 *
 * Sets the widget's width.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_width (LrgWidget *self,
                           gfloat     width);

/**
 * lrg_widget_get_height:
 * @self: an #LrgWidget
 *
 * Gets the widget's height.
 *
 * Returns: The height
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_height (LrgWidget *self);

/**
 * lrg_widget_set_height:
 * @self: an #LrgWidget
 * @height: the height
 *
 * Sets the widget's height.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_height (LrgWidget *self,
                            gfloat     height);

/**
 * lrg_widget_set_position:
 * @self: an #LrgWidget
 * @x: the X position
 * @y: the Y position
 *
 * Sets both the X and Y position at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_position (LrgWidget *self,
                              gfloat     x,
                              gfloat     y);

/**
 * lrg_widget_set_size:
 * @self: an #LrgWidget
 * @width: the width
 * @height: the height
 *
 * Sets both the width and height at once.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_size (LrgWidget *self,
                          gfloat     width,
                          gfloat     height);

/* ==========================================================================
 * World Coordinates
 * ========================================================================== */

/**
 * lrg_widget_get_world_x:
 * @self: an #LrgWidget
 *
 * Gets the widget's absolute X position in world coordinates.
 * This accounts for all parent positions up the hierarchy.
 *
 * Returns: The absolute X position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_world_x (LrgWidget *self);

/**
 * lrg_widget_get_world_y:
 * @self: an #LrgWidget
 *
 * Gets the widget's absolute Y position in world coordinates.
 * This accounts for all parent positions up the hierarchy.
 *
 * Returns: The absolute Y position
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_widget_get_world_y (LrgWidget *self);

/* ==========================================================================
 * State
 * ========================================================================== */

/**
 * lrg_widget_get_visible:
 * @self: an #LrgWidget
 *
 * Gets whether the widget is visible.
 * Invisible widgets are not drawn.
 *
 * Returns: %TRUE if visible
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_visible (LrgWidget *self);

/**
 * lrg_widget_set_visible:
 * @self: an #LrgWidget
 * @visible: whether to show the widget
 *
 * Sets whether the widget is visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_visible (LrgWidget *self,
                             gboolean   visible);

/**
 * lrg_widget_get_enabled:
 * @self: an #LrgWidget
 *
 * Gets whether the widget is enabled.
 * Disabled widgets do not respond to input events.
 *
 * Returns: %TRUE if enabled
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_get_enabled (LrgWidget *self);

/**
 * lrg_widget_set_enabled:
 * @self: an #LrgWidget
 * @enabled: whether to enable the widget
 *
 * Sets whether the widget is enabled.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_set_enabled (LrgWidget *self,
                             gboolean   enabled);

/* ==========================================================================
 * Hierarchy
 * ========================================================================== */

/**
 * lrg_widget_get_parent:
 * @self: an #LrgWidget
 *
 * Gets the parent container of this widget.
 *
 * Returns: (transfer none) (nullable): The parent #LrgContainer, or %NULL
 */
LRG_AVAILABLE_IN_ALL
LrgContainer * lrg_widget_get_parent (LrgWidget *self);

/* ==========================================================================
 * Hit Testing
 * ========================================================================== */

/**
 * lrg_widget_contains_point:
 * @self: an #LrgWidget
 * @x: the X coordinate in world space
 * @y: the Y coordinate in world space
 *
 * Checks if the given point is within the widget's bounds.
 *
 * Returns: %TRUE if the point is inside the widget
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_contains_point (LrgWidget *self,
                                    gfloat     x,
                                    gfloat     y);

/* ==========================================================================
 * Virtual Method Wrappers
 * ========================================================================== */

/**
 * lrg_widget_draw:
 * @self: an #LrgWidget
 *
 * Draws the widget by calling the virtual draw() method.
 * Only draws if the widget is visible.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_draw (LrgWidget *self);

/**
 * lrg_widget_measure:
 * @self: an #LrgWidget
 * @preferred_width: (out): location to store preferred width
 * @preferred_height: (out): location to store preferred height
 *
 * Calculates the widget's preferred size by calling the
 * virtual measure() method.
 */
LRG_AVAILABLE_IN_ALL
void lrg_widget_measure (LrgWidget *self,
                         gfloat    *preferred_width,
                         gfloat    *preferred_height);

/**
 * lrg_widget_handle_event:
 * @self: an #LrgWidget
 * @event: the UI event to handle
 *
 * Handles a UI event by calling the virtual handle_event() method.
 * Only processes events if the widget is visible and enabled.
 *
 * Returns: %TRUE if the event was consumed
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_widget_handle_event (LrgWidget        *self,
                                  const LrgUIEvent *event);

G_END_DECLS
