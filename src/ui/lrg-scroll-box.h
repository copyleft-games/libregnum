/* lrg-scroll-box.h
 *
 * Copyright 2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * LrgScrollBox: A clipped, scrollable viewport onto one child with spring-smoothed scrolling.
 */

#pragma once

#if !defined(LIBREGNUM_INSIDE) && !defined(LIBREGNUM_COMPILATION)
#error "Only <libregnum.h> can be included directly."
#endif

#include <glib-object.h>
#include "../lrg-version.h"
#include "lrg-container.h"

G_BEGIN_DECLS

#define LRG_TYPE_SCROLL_BOX (lrg_scroll_box_get_type ())

LRG_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (LrgScrollBox, lrg_scroll_box, LRG, SCROLL_BOX, LrgContainer)

/**
 * lrg_scroll_box_new:
 *
 * Creates an empty scroll box that scrolls vertically. Put the content
 * in with lrg_scroll_box_set_child().
 *
 * Returns: (transfer full): a new #LrgScrollBox
 */
LRG_AVAILABLE_IN_ALL
LrgScrollBox * lrg_scroll_box_new (void);

/**
 * lrg_scroll_box_set_child:
 * @self: an #LrgScrollBox
 * @child: (nullable): the content widget, or %NULL to remove it
 *
 * Sets the one widget shown through the viewport. It is given its
 * preferred size (stretched to the viewport on axes that do not
 * scroll) and moved as the box scrolls. Use this rather than
 * lrg_container_add_child().
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_child (LrgScrollBox *self,
                               LrgWidget    *child);

/**
 * lrg_scroll_box_get_child:
 * @self: an #LrgScrollBox
 *
 * Returns: (transfer none) (nullable): the content widget
 */
LRG_AVAILABLE_IN_ALL
LrgWidget * lrg_scroll_box_get_child (LrgScrollBox *self);

/**
 * lrg_scroll_box_get_scroll_x:
 * @self: an #LrgScrollBox
 *
 * Returns: the horizontal offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_scroll_box_get_scroll_x (LrgScrollBox *self);

/**
 * lrg_scroll_box_set_scroll_x:
 * @self: an #LrgScrollBox
 * @scroll_x: new horizontal offset, clamped to the scroll range
 *
 * Jumps horizontally without animation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_scroll_x (LrgScrollBox *self,
                                  gfloat        scroll_x);

/**
 * lrg_scroll_box_get_scroll_y:
 * @self: an #LrgScrollBox
 *
 * Returns: the vertical offset currently displayed
 */
LRG_AVAILABLE_IN_ALL
gfloat lrg_scroll_box_get_scroll_y (LrgScrollBox *self);

/**
 * lrg_scroll_box_set_scroll_y:
 * @self: an #LrgScrollBox
 * @scroll_y: new vertical offset, clamped to the scroll range
 *
 * Jumps vertically without animation.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_scroll_y (LrgScrollBox *self,
                                  gfloat        scroll_y);

/**
 * lrg_scroll_box_get_hscroll:
 * @self: an #LrgScrollBox
 *
 * Returns: %TRUE if the content can scroll horizontally
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scroll_box_get_hscroll (LrgScrollBox *self);

/**
 * lrg_scroll_box_set_hscroll:
 * @self: an #LrgScrollBox
 * @enabled: allow horizontal scrolling; when %FALSE (the default) the
 *   child is as wide as the viewport
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_hscroll (LrgScrollBox *self,
                                 gboolean      enabled);

/**
 * lrg_scroll_box_get_vscroll:
 * @self: an #LrgScrollBox
 *
 * Returns: %TRUE if the content can scroll vertically
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scroll_box_get_vscroll (LrgScrollBox *self);

/**
 * lrg_scroll_box_set_vscroll:
 * @self: an #LrgScrollBox
 * @enabled: allow vertical scrolling (default %TRUE); when %FALSE the
 *   child is as tall as the viewport
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_vscroll (LrgScrollBox *self,
                                 gboolean      enabled);

/**
 * lrg_scroll_box_get_kinetic:
 * @self: an #LrgScrollBox
 *
 * Returns: %TRUE if scrolling glides and rubber-bands
 */
LRG_AVAILABLE_IN_ALL
gboolean lrg_scroll_box_get_kinetic (LrgScrollBox *self);

/**
 * lrg_scroll_box_set_kinetic:
 * @self: an #LrgScrollBox
 * @kinetic: %TRUE (default) for spring-smoothed scrolling with a
 *   rubber-band overshoot at the ends; %FALSE jumps immediately
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_set_kinetic (LrgScrollBox *self,
                                 gboolean      kinetic);

/**
 * lrg_scroll_box_scroll_to:
 * @self: an #LrgScrollBox
 * @x: horizontal offset
 * @y: vertical offset
 * @animate: glide there with the scroll spring instead of jumping
 *
 * Scrolls to an offset, clamped to the scroll range.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_scroll_to (LrgScrollBox *self,
                               gfloat        x,
                               gfloat        y,
                               gboolean      animate);

/**
 * lrg_scroll_box_get_max_scroll:
 * @self: an #LrgScrollBox
 * @x: (out) (optional): largest horizontal offset
 * @y: (out) (optional): largest vertical offset
 *
 * The scroll range: content size minus viewport size, never negative.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_get_max_scroll (LrgScrollBox *self,
                                    gfloat       *x,
                                    gfloat       *y);

/**
 * lrg_scroll_box_scroll_to_widget:
 * @self: an #LrgScrollBox
 * @descendant: a widget inside the content
 *
 * Scrolls (animated) by the smallest amount that brings @descendant
 * fully into view; if it is larger than the viewport its top-left
 * corner is shown. The box also does this by itself when keyboard or
 * gamepad focus moves to a descendant.
 */
LRG_AVAILABLE_IN_ALL
void lrg_scroll_box_scroll_to_widget (LrgScrollBox *self,
                                      LrgWidget    *descendant);

G_END_DECLS
