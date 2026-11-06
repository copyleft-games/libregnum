/* lrg-widget-private.h
 *
 * Copyright 2025-2026 Zach Podbielniak
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Private widget helpers shared within src/ui.
 */

#pragma once

#include "lrg-widget.h"

G_BEGIN_DECLS

/*
 * _lrg_widget_set_parent:
 * @self: an #LrgWidget
 * @parent: (nullable): the new parent, or %NULL to unparent
 *
 * Sets the widget's parent container. Only #LrgContainer calls this.
 */
void _lrg_widget_set_parent (LrgWidget    *self,
                             LrgContainer *parent);

/*
 * _lrg_widget_real_paint / _lrg_widget_real_draw:
 *
 * LrgWidget's default paint() and draw(). Exposed so the dispatcher
 * can tell which of the two a subclass overrides.
 */
void _lrg_widget_real_paint (LrgWidget    *self,
                             LrgPaintList *list);
void _lrg_widget_real_draw  (LrgWidget    *self);

/*
 * _lrg_container_default_paint / _lrg_container_default_draw:
 *
 * LrgContainer's defaults (paint/draw the children). Defined in
 * lrg-container.c.
 */
void _lrg_container_default_paint (LrgWidget    *self,
                                   LrgPaintList *list);
void _lrg_container_default_draw  (LrgWidget    *self);

/*
 * _lrg_widget_set_rect_quiet:
 *
 * Assigns the rectangle with property notifications but without
 * queueing layout (used by lrg_widget_allocate()).
 */
void _lrg_widget_set_rect_quiet (LrgWidget *self,
                                 gfloat     x,
                                 gfloat     y,
                                 gfloat     width,
                                 gfloat     height);

/*
 * _lrg_widget_clear_needs_layout:
 *
 * Clears the layout flag without arranging.
 */
void _lrg_widget_clear_needs_layout (LrgWidget *self);

/*
 * _lrg_box_layout:
 * @self: a container
 * @vertical: stack top-to-bottom (TRUE) or left-to-right (FALSE)
 * @homogeneous: give every child the same share of the main axis
 *
 * Shared VBox/HBox layout: each visible child gets a slot as long as
 * its preferred size plus margin, spare space is split by fill weight
 * (or equally when homogeneous), and the child is placed in its slot
 * with lrg_widget_place_in_slot() (margin, alignment, min/max).
 */
void _lrg_box_layout (LrgContainer *self,
                      gboolean      vertical,
                      gboolean      homogeneous);

/*
 * _lrg_box_measure:
 *
 * Preferred size of a box: children's preferred sizes plus margins,
 * spacing between them and padding around them.
 */
void _lrg_box_measure (LrgContainer *self,
                       gboolean      vertical,
                       gboolean      homogeneous,
                       gfloat       *width,
                       gfloat       *height);

/*
 * _lrg_widget_outer_size:
 *
 * Preferred size including the margin.
 */
void _lrg_widget_outer_size (LrgWidget *self,
                             gfloat    *width,
                             gfloat    *height);

G_END_DECLS
