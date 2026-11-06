/* lrg-custom-widget.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgCustomWidget: a widget measured, painted, stepped and fed events
 * by the application, through signals or a C function table, without
 * subclassing.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-widget.h"

G_BEGIN_DECLS

/* ==========================================================================
 * LrgCustomWidgetSize
 * ========================================================================== */

#define LRG_TYPE_CUSTOM_WIDGET_SIZE (lrg_custom_widget_size_get_type ())

/**
 * LrgCustomWidgetSize:
 * @width: preferred width in canvas units (margin excluded)
 * @height: preferred height in canvas units
 *
 * The in/out size of #LrgCustomWidget::measure. It arrives filled with
 * #LrgCustomWidget:preferred-width and #LrgCustomWidget:preferred-height;
 * handlers overwrite what they know.
 */
typedef struct
{
	gfloat width;
	gfloat height;
} LrgCustomWidgetSize;

LRG_AVAILABLE_IN_ALL
GType lrg_custom_widget_size_get_type (void) G_GNUC_CONST;

/**
 * lrg_custom_widget_size_copy:
 * @self: an #LrgCustomWidgetSize
 *
 * Returns: (transfer full): a copy of @self
 */
LRG_AVAILABLE_IN_ALL
LrgCustomWidgetSize * lrg_custom_widget_size_copy (const LrgCustomWidgetSize *self);

/**
 * lrg_custom_widget_size_free:
 * @self: (nullable): an #LrgCustomWidgetSize
 */
LRG_AVAILABLE_IN_ALL
void lrg_custom_widget_size_free (LrgCustomWidgetSize *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (LrgCustomWidgetSize, lrg_custom_widget_size_free)

/* ==========================================================================
 * LrgCustomWidget
 * ========================================================================== */

#define LRG_TYPE_CUSTOM_WIDGET (lrg_custom_widget_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgCustomWidget, lrg_custom_widget, LRG, CUSTOM_WIDGET, LrgWidget)

/**
 * LrgCustomWidgetFuncs:
 * @measure: (nullable): adjusts @size (prefilled with the preferred
 *   size properties); @avail_width / @avail_height are the current
 *   allocation (0 before the first layout)
 * @paint: (nullable): records the widget into @list at its world
 *   rectangle @x, @y, @width, @height
 * @step: (nullable): advances the widget's own animation by
 *   @delta_time seconds; returns %TRUE while still moving
 * @event: (nullable): handles @event; returns %TRUE to consume it
 *
 * A C function table for #LrgCustomWidget, the fast alternative to the
 * signals. Each function runs before the matching signal; an @event
 * function returning %TRUE stops the "event" signal. Every function
 * gets the user data given to lrg_custom_widget_set_funcs().
 */
typedef struct
{
	void     (*measure) (LrgCustomWidget     *widget,
	                     gfloat               avail_width,
	                     gfloat               avail_height,
	                     LrgCustomWidgetSize *size,
	                     gpointer             user_data);
	void     (*paint)   (LrgCustomWidget     *widget,
	                     LrgPaintList        *list,
	                     gfloat               x,
	                     gfloat               y,
	                     gfloat               width,
	                     gfloat               height,
	                     gpointer             user_data);
	gboolean (*step)    (LrgCustomWidget     *widget,
	                     gfloat               delta_time,
	                     gpointer             user_data);
	gboolean (*event)   (LrgCustomWidget     *widget,
	                     const LrgUIEvent    *event,
	                     gpointer             user_data);

	/*< private >*/
	gpointer _reserved[4];
} LrgCustomWidgetFuncs;

/**
 * lrg_custom_widget_new:
 *
 * Creates a custom widget with no callbacks: it measures to its
 * preferred size properties (0 x 0) and paints nothing until a
 * function table or signal handlers are added.
 *
 * Returns: (transfer full): a new #LrgCustomWidget
 */
LRG_AVAILABLE_IN_ALL
LrgCustomWidget * lrg_custom_widget_new (void);

/**
 * lrg_custom_widget_set_funcs:
 * @self: an #LrgCustomWidget
 * @funcs: (nullable): the function table, copied; %NULL removes it
 * @user_data: data passed to every function
 * @destroy: (nullable): frees @user_data when the table is replaced or
 *   the widget is disposed
 *
 * Installs a C function table (see #LrgCustomWidgetFuncs) and queues a
 * layout.
 */
LRG_AVAILABLE_IN_ALL
void lrg_custom_widget_set_funcs (LrgCustomWidget            *self,
                                  const LrgCustomWidgetFuncs *funcs,
                                  gpointer                    user_data,
                                  GDestroyNotify              destroy);

/**
 * lrg_custom_widget_get_preferred_width:
 * @self: an #LrgCustomWidget
 *
 * Returns: the base preferred width handed to measure
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_custom_widget_get_preferred_width (LrgCustomWidget *self);

/**
 * lrg_custom_widget_set_preferred_width:
 * @self: an #LrgCustomWidget
 * @width: preferred width in canvas units
 *
 * The size the widget asks for when no measure callback changes it.
 */
LRG_AVAILABLE_IN_ALL
void lrg_custom_widget_set_preferred_width (LrgCustomWidget *self,
                                            gfloat           width);

/**
 * lrg_custom_widget_get_preferred_height:
 * @self: an #LrgCustomWidget
 *
 * Returns: the base preferred height handed to measure
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_custom_widget_get_preferred_height (LrgCustomWidget *self);

/**
 * lrg_custom_widget_set_preferred_height:
 * @self: an #LrgCustomWidget
 * @height: preferred height in canvas units
 */
LRG_AVAILABLE_IN_ALL
void lrg_custom_widget_set_preferred_height (LrgCustomWidget *self,
                                             gfloat           height);

/**
 * lrg_custom_widget_get_paint_box:
 * @self: an #LrgCustomWidget
 *
 * Returns: whether the BOX look is painted under the custom paint
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_custom_widget_get_paint_box (LrgCustomWidget *self);

/**
 * lrg_custom_widget_set_paint_box:
 * @self: an #LrgCustomWidget
 * @paint_box: %TRUE to paint the themed BOX surface (SURFACE fill,
 *   border, corner radius; hover and press variants) before the paint
 *   callbacks
 */
LRG_AVAILABLE_IN_ALL
void lrg_custom_widget_set_paint_box (LrgCustomWidget *self,
                                      gboolean         paint_box);

G_END_DECLS
